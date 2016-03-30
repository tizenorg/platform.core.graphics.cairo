/* Copyright 2015 Samsung Electronics Co., LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/***************************************************************************
 * Objects in a scene.
 ***************************************************************************/

#include "scene_object.h"

#include "objects/components/camera.h"
#include "objects/components/camera_rig.h"
#include "objects/components/eye_pointee_holder.h"
#include "objects/components/render_data.h"
#include "util/gvr_log.h"
#include "mesh.h"
#include <cfloat>

namespace gvr {
SceneObject::SceneObject() :
        HybridObject(), name_(""), children_(), visible_(true), in_frustum_(
                false), query_currently_issued_(false), vis_count_(0), lod_min_range_(
                0), lod_max_range_(FLT_MAX /*MAXFLOAT*/), using_lod_(false), bounding_volume_dirty_(
                true) {
//MAXFLOAT changed to FLT_MAX from <cfloat>

    // Occlusion query setup
#if _GVRF_USE_GLES3_
    queries_ = new GLuint[1];
    glGenQueries(1, queries_);
#endif
}

SceneObject::~SceneObject() {
#if _GVRF_USE_GLES3_
    delete queries_;
#endif
}

void SceneObject::attachTransform(SceneObject* self, Transform* transform) {
    if (transform_) {
        detachTransform();
    }
    SceneObject* owner_object(transform->owner_object());
    if (owner_object) {
        owner_object->detachRenderData();
    }
    transform_ = transform;
    transform_->set_owner_object(self);
    dirtyHierarchicalBoundingVolume();
}

void SceneObject::detachTransform() {
    if (transform_) {
        transform_->removeOwnerObject();
        transform_ = NULL;
    }
    dirtyHierarchicalBoundingVolume();
}

void SceneObject::attachRenderData(SceneObject* self, RenderData* render_data) {
    if (render_data_) {
        detachRenderData();
    }
    SceneObject* owner_object(render_data->owner_object());
    if (owner_object) {
        owner_object->detachRenderData();
    }
    render_data_ = render_data;
    render_data->set_owner_object(self);
    dirtyHierarchicalBoundingVolume();
}

void SceneObject::detachRenderData() {
    if (render_data_) {
        render_data_->removeOwnerObject();
        render_data_ = NULL;
    }
    dirtyHierarchicalBoundingVolume();
}

void SceneObject::attachCamera(SceneObject* self, Camera* camera) {
    if (camera_) {
        detachCamera();
    }
    SceneObject* owner_object(camera->owner_object());
    if (owner_object) {
        owner_object->detachCamera();
    }
    camera_ = camera;
    camera_->set_owner_object(self);
}

void SceneObject::detachCamera() {
    if (camera_) {
        camera_->removeOwnerObject();
        camera_ = NULL;
    }
}

void SceneObject::attachCameraRig(SceneObject* self, CameraRig* camera_rig) {
    if (camera_rig_) {
        detachCameraRig();
    }
    SceneObject* owner_object(camera_rig->owner_object());
    if (owner_object) {
        owner_object->detachCameraRig();
    }
    camera_rig_ = camera_rig;
    camera_rig_->set_owner_object(self);
}

void SceneObject::detachCameraRig() {
    if (camera_rig_) {
        camera_rig_->removeOwnerObject();
        camera_rig_ = NULL;
    }
}

void SceneObject::attachEyePointeeHolder(SceneObject* self,
        EyePointeeHolder* eye_pointee_holder) {
    if (eye_pointee_holder_) {
        detachEyePointeeHolder();
    }
    SceneObject* owner_object(eye_pointee_holder->owner_object());
    if (owner_object) {
        owner_object->detachEyePointeeHolder();
    }
    eye_pointee_holder_ = eye_pointee_holder;
    eye_pointee_holder_->set_owner_object(self);
}

void SceneObject::detachEyePointeeHolder() {
    if (eye_pointee_holder_) {
        eye_pointee_holder_->removeOwnerObject();
        eye_pointee_holder_ = NULL;
    }
}

void SceneObject::addChildObject(SceneObject* self, SceneObject* child) {
    for (SceneObject* parent = parent_; parent; parent = parent->parent_) {
        if (child == parent) {
            std::string error =
                    "SceneObject::addChildObject() : cycle of scene objects is not allowed.";
            LOGE("%s", error.c_str());
            throw error;
        }
    }
    {
        std::lock_guard < std::mutex > lock(children_mutex_);
        children_.push_back(child);
    }
    child->parent_ = self;
    Transform* const t = child->transform();
    if (nullptr != t) {
        t->invalidate(false);
    }
    dirtyHierarchicalBoundingVolume();
}

void SceneObject::removeChildObject(SceneObject* child) {
    if (child->parent_ == this) {
        {
            std::lock_guard < std::mutex > lock(children_mutex_);
            children_.erase(std::remove(children_.begin(), children_.end(), child), children_.end());
        }
        child->parent_ = NULL;
    }

    Transform* const t = child->transform();
    if (nullptr != t) {
        t->invalidate(false);
    }
    dirtyHierarchicalBoundingVolume();
}

int SceneObject::getChildrenCount() const {
    return children_.size();
}

SceneObject* SceneObject::getChildByIndex(int index) {
    if (index < children_.size()) {
        return children_[index];
    } else {
        std::string error = "SceneObject::getChildByIndex() : Out of index.";
        throw error;
    }
}

void SceneObject::set_visible(bool visibility = true) {

    //HACK
    //If checked every frame, queries may return
    //an inconsistent result when used with bounding boxes.

    //We need to make sure that the object's visibility status is consistent before
    //changing the status to avoid flickering artifacts.

    if (visibility == true)
        vis_count_++;
    else
        vis_count_--;

    if (vis_count_ > check_frames_) {
        visible_ = true;
        vis_count_ = 0;
    } else if (vis_count_ < (-1 * check_frames_)) {
        visible_ = false;
        vis_count_ = 0;
    }
}

bool SceneObject::isColliding(SceneObject *scene_object) {

    //Get the transformed bounding boxes in world coordinates and check if they intersect
    //Transformation is done by the getTransformedBoundingBoxInfo method in the Mesh class

    float this_object_bounding_box[6], check_object_bounding_box[6];

    Transform* t = this->render_data()->owner_object()->transform();
    if (nullptr == t) {
        LOGE("isColliding: no transform for this scene object");
        return false;
    }
    glm::mat4 this_object_model_matrix = t->getModelMatrix();
    this->render_data()->mesh()->getTransformedBoundingBoxInfo(
            &this_object_model_matrix, this_object_bounding_box);

    t = scene_object->render_data()->owner_object()->transform();
    if (nullptr == t) {
        LOGE("isColliding: no transform for target scene object");
        return false;
    }
    glm::mat4 check_object_model_matrix = t->getModelMatrix();
    scene_object->render_data()->mesh()->getTransformedBoundingBoxInfo(
            &check_object_model_matrix, check_object_bounding_box);

    bool result = (this_object_bounding_box[3] > check_object_bounding_box[0]
            && this_object_bounding_box[0] < check_object_bounding_box[3]
            && this_object_bounding_box[4] > check_object_bounding_box[1]
            && this_object_bounding_box[1] < check_object_bounding_box[4]
            && this_object_bounding_box[5] > check_object_bounding_box[2]
            && this_object_bounding_box[2] < check_object_bounding_box[5]);

    return result;
}

/**
 * Test the input ray against the scene objects HBV.
 *
 * This method uses the algorithm described in the paper:
 *
 * An Efficient and Robust Ray–Box Intersection Algorithm by
 * Amy Williams, Steve Barrus, R. Keith Morley and Peter Shirley.
 *
 * http://people.csail.mit.edu/amy/papers/box-jgt.pdf
 */
bool SceneObject::intersectsBoundingVolume(float rox, float roy, float roz,
        float rdx, float rdy, float rdz) {
    BoundingVolume bounding_volume_ = getBoundingVolume();

    float tmin, tmax, tymin, tymax, tzmin, tzmax;

    int sign[3];
    glm::vec3 invdir;
    invdir.x = 1 / rdx;
    invdir.y = 1 / rdy;
    invdir.z = 1 / rdz;
    sign[0] = (invdir.x < 0);
    sign[1] = (invdir.y < 0);
    sign[2] = (invdir.z < 0);

    glm::vec3 bounds[2];
    bounds[0] = bounding_volume_.min_corner();
    bounds[1] = bounding_volume_.max_corner();

    tmin = (bounds[sign[0]].x - rox) * invdir.x;
    tmax = (bounds[1 - sign[0]].x - rox) * invdir.x;
    tymin = (bounds[sign[1]].y - roy) * invdir.y;
    tymax = (bounds[1 - sign[1]].y - roy) * invdir.y;

    if ((tmin > tymax) || (tymin > tmax))
        return false;

    if (tymin > tmin)
        tmin = tymin;
    if (tymax < tmax)
        tmax = tymax;

    tzmin = (bounds[sign[2]].z - roz) * invdir.z;
    tzmax = (bounds[1 - sign[2]].z - roz) * invdir.z;

    if ((tmin > tzmax) || (tzmin > tmax))
        return false;

    if (tzmin > tmin)
        tmin = tzmin;
    if (tzmax < tmax)
        tmax = tzmax;

    if (tmin < 0 && tmax < 0) {
        return false;
    }

    return true;
}

void SceneObject::dirtyHierarchicalBoundingVolume() {
    if (bounding_volume_dirty_) {
        return;
    }

    bounding_volume_dirty_ = true;

    if (parent_ != NULL) {
        parent_->dirtyHierarchicalBoundingVolume();
    }
}

BoundingVolume& SceneObject::getBoundingVolume() {
    if (!bounding_volume_dirty_) {
        return transformed_bounding_volume_;
    }

    // Calculate the new bounding volume from itself and all its children
    // 1. Start from its own mesh's bounding volume if there is any
    if (render_data_ != NULL && render_data_->mesh() != NULL) {
        // Future optimization:
        // If the mesh and transform are still valid, don't need to recompute the mesh_bounding_volume
        // if (!render_data_->mesh()->hasBoundingVolume()
        // || !transform_->isModelMatrixValid()) {
        mesh_bounding_volume.transform(
                render_data_->mesh()->getBoundingVolume(),
                transform_->getModelMatrix());
        //	}
        transformed_bounding_volume_ = mesh_bounding_volume;
    }

    // 2. Aggregate with all its children's bounding volumes
    std::vector<SceneObject*> childrenCopy = children();
    for (auto it = childrenCopy.begin(); it != childrenCopy.end(); ++it) {
        transformed_bounding_volume_.expand((*it)->getBoundingVolume());
    }

    bounding_volume_dirty_ = false;

    return transformed_bounding_volume_;
}

float planeDistanceToPoint(float plane[4], glm::vec3 &compare_point) {
    glm::vec3 normal = glm::vec3(plane[0], plane[1], plane[2]);
    glm::normalize(normal);
    float distance_to_origin = plane[3];
    float distance = glm::dot(compare_point, normal) + distance_to_origin;

    return distance;
}

bool SceneObject::checkSphereVsFrustum(float frustum[6][4],
        BoundingVolume &sphere) {
    glm::vec3 center = sphere.center();
    float radius = sphere.radius();

    for (int i = 0; i < 6; i++) {
        float distance = planeDistanceToPoint(frustum[i], center);
        if (distance < -radius) {
            return false; // outside
        }
    }

    return true; // fully inside
}

enum AABB_STATE {
    OUTSIDE, INTERSECT, INSIDE
};

// frustumCull() return 3 possible values:
// 0 when the HBV of the object is completely outside the frustum: cull itself and all its children out
// 1 when the HBV of the object is intersecting the frustum but the object itself is not: cull it out and continue culling test with its children
// 2 when the HBV of the object is intersecting the frustum and the mesh BV of the object are intersecting (inside) the frustum: render itself and continue culling test with its children
// 3 when the HBV of the object is completely inside the frustum: render itself and all its children without further culling test
int SceneObject::frustumCull(Camera *camera, const float frustum[6][4],
        int& planeMask) {
    if (!visible_) {
        if (DEBUG_RENDERER) {
            LOGD("FRUSTUM: not visible, cull out %s and all its children\n",
                    name_.c_str());
        }

        return 0;
    }

    // 1. Check if the bounding volume intersects with or inside the view frustum
    BoundingVolume bounding_volume_ = getBoundingVolume();
    char outPlaneMask;
    int checkResult = checkAABBVsFrustumOpt(frustum, bounding_volume_,
            planeMask);
    // int checkResult = checkSphereVsFrustum(frustum, bounding_volume_);

    // Cull out the object and all its children if its bounding volume is completely outside the frustum
    if (checkResult == OUTSIDE) {
        if (DEBUG_RENDERER) {
            LOGD(
                    "FRUSTUM: HBV completely outside frustum, cull out %s and all its children\n",
                    name_.c_str());
        }

        return 0;
    }

    if (checkResult == INSIDE) {
        if (DEBUG_RENDERER) {
            LOGD(
                    "FRUSTUM: HBV completely inside frustum, render %s and all its children\n",
                    name_.c_str());
        }

        return 3;
    }

    // 2. Skip the empty objects with no render data
    if (render_data_ == NULL || render_data_->pass(0)->material() == NULL) {
        if (DEBUG_RENDERER) {
            LOGD("FRUSTUM: no render data skip %s\n", name_.c_str());
        }

        return 1;
    }

    // 3. Check if the object against the Level-of-details
    // Transform the bounding sphere
    glm::vec4 transformed_sphere_center(bounding_volume_.center(), 1.0f);

    // Calculate distance from camera
    glm::vec3 camera_position = camera->owner_object()->transform()->position();
    glm::vec4 position(camera_position, 1.0f);
    glm::vec4 difference = transformed_sphere_center - position;
    float distance = glm::dot(difference, difference);

    // this distance will be used when sorting transparent objects
    render_data_->set_camera_distance(distance);

    // if the object is not in the correct LOD level, cull out itself and all its children
    if (!inLODRange(distance)) {
        if (DEBUG_RENDERER) {
            LOGD(
                    "FRUSTUM: not in lod range, cull out %s and all its children\n",
                    name_.c_str());
        }

        return 0;
    }

    // 4. Check if the object itself is intersecting with or inside the frustum
    size_t size;
    {
        std::lock_guard < std::mutex > lock(children_mutex_);
        size = children_.size();
    }
    if (0 < size) {
        int tempMask = planeMask;
        checkResult = checkAABBVsFrustumOpt(frustum, mesh_bounding_volume,
                tempMask);
        //	checkResult = checkSphereVsFrustum(frustum, mesh_bounding_volume);
    }

    // if the object is not in the frustum, cull out itself but continue testing its children
    if (DEBUG_RENDERER) {
        if (checkResult == OUTSIDE) {
            LOGD("FRUSTUM: mesh not in frustum, cull out %s\n", name_.c_str());
        } else {
            LOGD("FRUSTUM: mesh in frustum, render %s\n", name_.c_str());
        }
    }

    return checkResult == 0 ? 1 : 2;
}

// Test if a AABB bounding volume is completely outside, inside or intersecting the frustum
// Test each of the eight vertices of the AABB bounding volume against each of the six frustum planes:
// If the AABB is completely outside any frustum plane, return 0 indicating the AABB is completely outside the whole frustum;
// If the any vertex of the AABB is outside a frustum plane, return 1 indicating the AABB is intersecting the frustum;
// If the AABB is completely inside all frustum planes, return 2 indicating the AABB is completely inside the frustum.
int SceneObject::checkAABBVsFrustumOpt(const float frustum[6][4],
        BoundingVolume &bounding_volume, int& planeMask) {
    glm::vec3 min_corner = bounding_volume.min_corner();
    glm::vec3 max_corner = bounding_volume.max_corner();

    float Xmin = min_corner[0];
    float Ymin = min_corner[1];
    float Zmin = min_corner[2];
    float Xmax = max_corner[0];
    float Ymax = max_corner[1];
    float Zmax = max_corner[2];

    bool isCompleteInside = true;

    for (int p = 0; p < 6; p++) {
        //skip current plane if the corresponding planeMask is set
        if ((planeMask >> p) & 1) {
            if (DEBUG_RENDERER) {
                LOGD("PLANE %d MASKED", p);
            }
            continue;
        }

        int count = 0;
        if (frustum[p][0] * (Xmin) + frustum[p][1] * (Ymin)
                + frustum[p][2] * (Zmin) + frustum[p][3] > 0) {
            count++;
        }

        if (frustum[p][0] * (Xmax) + frustum[p][1] * (Ymin)
                + frustum[p][2] * (Zmin) + frustum[p][3] > 0) {
            count++;
        }

        if (frustum[p][0] * (Xmin) + frustum[p][1] * (Ymax)
                + frustum[p][2] * (Zmin) + frustum[p][3] > 0) {
            count++;
        }

        if (frustum[p][0] * (Xmax) + frustum[p][1] * (Ymax)
                + frustum[p][2] * (Zmin) + frustum[p][3] > 0) {
            count++;
        }

        if (frustum[p][0] * (Xmin) + frustum[p][1] * (Ymin)
                + frustum[p][2] * (Zmax) + frustum[p][3] > 0) {
            count++;
        }

        if (frustum[p][0] * (Xmax) + frustum[p][1] * (Ymin)
                + frustum[p][2] * (Zmax) + frustum[p][3] > 0) {
            count++;
        }

        if (frustum[p][0] * (Xmin) + frustum[p][1] * (Ymax)
                + frustum[p][2] * (Zmax) + frustum[p][3] > 0) {
            count++;
        }

        if (frustum[p][0] * (Xmax) + frustum[p][1] * (Ymax)
                + frustum[p][2] * (Zmax) + frustum[p][3] > 0) {
            count++;
        }

        // All vertices are completely outside the frustum plane
        if (count == 0) {
            return OUTSIDE;
        }

        // If any vertex is outside the frustum plane, it cannot be completely inside the whole frustum
        if (count < 8) {
            isCompleteInside = false;
        }
        // If all vertices are inside the frustum plane, mask this plane so we can skip testing all its children against it
        else {
            planeMask = planeMask | (1 << p);
        }
    }

    return isCompleteInside ? INSIDE : INTERSECT;
}

bool SceneObject::checkAABBVsFrustumBasic(const float frustum[6][4],
        BoundingVolume &bounding_volume) {
    glm::vec3 min_corner = bounding_volume.min_corner();
    glm::vec3 max_corner = bounding_volume.max_corner();

    float Xmin = min_corner[0];
    float Ymin = min_corner[1];
    float Zmin = min_corner[2];
    float Xmax = max_corner[0];
    float Ymax = max_corner[1];
    float Zmax = max_corner[2];

    for (int p = 0; p < 6; p++) {
        if (frustum[p][0] * (Xmin) + frustum[p][1] * (Ymin)
                + frustum[p][2] * (Zmin) + frustum[p][3] > 0) {
            continue;
        }

        if (frustum[p][0] * (Xmax) + frustum[p][1] * (Ymin)
                + frustum[p][2] * (Zmin) + frustum[p][3] > 0) {
            continue;
        }

        if (frustum[p][0] * (Xmin) + frustum[p][1] * (Ymax)
                + frustum[p][2] * (Zmin) + frustum[p][3] > 0) {
            continue;
        }

        if (frustum[p][0] * (Xmax) + frustum[p][1] * (Ymax)
                + frustum[p][2] * (Zmin) + frustum[p][3] > 0) {
            continue;
        }

        if (frustum[p][0] * (Xmin) + frustum[p][1] * (Ymin)
                + frustum[p][2] * (Zmax) + frustum[p][3] > 0) {
            continue;
        }

        if (frustum[p][0] * (Xmax) + frustum[p][1] * (Ymin)
                + frustum[p][2] * (Zmax) + frustum[p][3] > 0) {
            continue;
        }

        if (frustum[p][0] * (Xmin) + frustum[p][1] * (Ymax)
                + frustum[p][2] * (Zmax) + frustum[p][3] > 0) {
            continue;
        }

        if (frustum[p][0] * (Xmax) + frustum[p][1] * (Ymax)
                + frustum[p][2] * (Zmax) + frustum[p][3] > 0) {
            continue;
        }

        return false;
    }
    return true;
}
}
