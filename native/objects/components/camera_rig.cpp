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
 * Holds left, right cameras and reacts to the rotation sensor.
 ***************************************************************************/

#include "camera_rig.h"

#include "glm/gtc/quaternion.hpp"

#include "objects/scene_object.h"
#include "objects/components/camera.h"
#include "objects/components/perspective_camera.h"
#include "util/gvr_time.h"

namespace gvr {

float CameraRig::default_camera_separation_distance_ = 0.062f;

CameraRig::CameraRig() :
        Component(), camera_rig_type_(DEFAULT_CAMERA_RIG_TYPE), left_camera_(), right_camera_(), center_camera_(), camera_separation_distance_(
                default_camera_separation_distance_), floats_(), vec2s_(), vec3s_(), vec4s_(), complementary_rotation_(), rotation_sensor_data_() {
}

CameraRig::~CameraRig() {
}

void CameraRig::attachLeftCamera(Camera* const left_camera) {
    Transform* const t = left_camera->owner_object()->transform();
    if (nullptr == t) {
        LOGE("attachLeftCamera error: no transform");
        return;
    }
    t->set_position(-camera_separation_distance_ * 0.5f, 0.0f, 0.0f);
    left_camera_ = left_camera;
}

void CameraRig::attachRightCamera(Camera* const right_camera) {
    Transform* const t = right_camera->owner_object()->transform();
    if (nullptr == t) {
        LOGE("attachRightCamera error: no transform");
        return;
    }
    t->set_position(camera_separation_distance_ * 0.5f, 0.0f, 0.0f);
    right_camera_ = right_camera;
}

/*
 * We want to create a center camera that encompasses the fov of both the left and right cameras.
 * To do this, we keep the camera at an x,y of 0, and move it in z.
 * To find z:
 *         \ left /|\ right/
 *          \ eye/ | \ eye/
 *           \  /  |  \  /
 *            \/   |   \/
 *             ----+---  <- ipd
 *              \  |  /
 *               \ | /
 *                \|/
 *                 z
 *
 * Let's look at one of the triangles that make the center camera:
 *
 *   ipd/2
 *   +---
 *   |  /
 *   | /
 *   |/
 *   z
 *
 * We know:
 *    opposite:  ipd/2
 *       theta:  fov_y / 2
 *
 * We need z (adjacent).
 *
 * tan(theta) = opposite / adjacent
 * adjacent = opposite * 1/tan(theta)
 * z = ipd/2 * 1/tan(fov_y/2)
 */
void CameraRig::attachCenterCamera(PerspectiveCamera* const center_camera) {
    Transform* const t = center_camera->owner_object()->transform();
    if (nullptr == t) {
        LOGE("attachCenterCamera error: no transform");
        return;
    }

    float half_ipd = camera_separation_distance_ * 0.5f;
    float theta = (center_camera->fov_y() * 0.5f);
    float tan_theta = tan(theta);
    float z = half_ipd * (1.0f/tan_theta);
    t->set_position(0.0f, 0.0f, z);
    center_camera_ = center_camera;
}

void CameraRig::reset() {
    complementary_rotation_ = glm::inverse(rotation_sensor_data_.quaternion());
}

void CameraRig::resetYaw() {
    glm::vec3 look_at = glm::rotate(rotation_sensor_data_.quaternion(),
            glm::vec3(0.0f, 0.0f, -1.0f));
    float yaw = atan2f(-look_at.x, -look_at.z);
    complementary_rotation_ = glm::angleAxis(-yaw, glm::vec3(0.0f, 1.0f, 0.0f));
}

void CameraRig::resetYawPitch() {
    glm::vec3 look_at = glm::rotate(rotation_sensor_data_.quaternion(),
            glm::vec3(0.0f, 0.0f, -1.0f));
    float pitch = atan2f(look_at.y,
            sqrtf(look_at.x * look_at.x + look_at.z * look_at.z));
    float yaw = atan2f(-look_at.x, -look_at.z);
    glm::quat quat = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    quat = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * quat;
    complementary_rotation_ = glm::inverse(quat);
}

void CameraRig::setRotationSensorData(long long time_stamp, float w, float x,
        float y, float z, float gyro_x, float gyro_y, float gyro_z) {
    rotation_sensor_data_.update(time_stamp, w, x, y, z, gyro_x, gyro_y, gyro_z);
}

void CameraRig::predict(float time) {
    return predict(time, rotation_sensor_data_);
}

void CameraRig::predict(float time, const RotationSensorData& rotationSensorData) {
    long long clock_time = getCurrentTime();
    float time_diff = (clock_time - rotationSensorData.time_stamp())
            / 1000000000.0f;

    glm::vec3 axis = rotationSensorData.gyro();
    //the magnitude of the gyro vector should be the angular velocity, rad/sec
    float angle = glm::length(axis);

    //normalize the axis
    if (angle != 0.0f) {
        axis /= angle;
    }

    setRotation(complementary_rotation_*rotationSensorData.quaternion());
}

glm::quat CameraRig::getPosePrediction(long long time_point) {
    return getPosePrediction(time_point, rotation_sensor_data_);
}

glm::quat CameraRig::getPosePrediction(long long time_point, const RotationSensorData& rotationSensorData)  {
    long long clock_time = getCurrentTime();
    long long mono_time = getNanoTime();

    //TODO: minimize variables, but in-place cal
    long long base_real_time = clock_time - mono_time; // read it again to minimize time jump
    long long time_point_real = base_real_time + time_point; // convert mono time to real
    float time_gap = (time_point_real - rotationSensorData.time_stamp())
                        / 1000000000.0f; // nano sec. to sec.
    if (time_gap < 0.0)
        time_gap = 0.0f;// should be future

    glm::vec3 axis = rotationSensorData.gyro();
    float angle = glm::length(axis);

    // if the motion is very small (usually because our body shakes), ignore the motion
    if (angle < 0.04f) {
        angle = 0.0f;
    }

    if (angle != 0.0f) {
        axis /= angle;
    }
    angle *= time_gap;

    glm::quat rotation = rotationSensorData.quaternion()
            * glm::angleAxis(angle, axis);
    glm::quat transform_rotation = complementary_rotation_ * rotation;

    return transform_rotation;
}

void CameraRig::setPoseState(float* pose_state) {
    glm::quat roation = glm::quat(pose_state[3], pose_state[0], pose_state[1],
            pose_state[2]);

    setRotation(roation);
}

void CameraRig::setRotation(const glm::quat& transform_rotation) {
    // Get head transform (a child of camera rig object)
    Transform* transform = getHeadTransform();

    if (camera_rig_type_ == FREE) {
        transform->set_rotation(transform_rotation);
    } else if (camera_rig_type_ == YAW_ONLY) {
        glm::vec3 look_at = glm::rotate(transform_rotation,
                glm::vec3(0.0f, 0.0f, -1.0f));
        float yaw = atan2f(-look_at.x, -look_at.z);
        transform->set_rotation(
                glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
    } else if (camera_rig_type_ == ROLL_FREEZE) {
        glm::vec3 look_at = glm::rotate(transform_rotation,
                glm::vec3(0.0f, 0.0f, -1.0f));
        float pitch = atan2f(look_at.y,
                sqrtf(look_at.x * look_at.x + look_at.z * look_at.z));
        float yaw = atan2f(-look_at.x, -look_at.z);
        transform->set_rotation(
                glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)));
        transform->rotateByAxis(yaw, 0.0f, 1.0f, 0.0f);
    } else if (camera_rig_type_ == FREEZE) {
        transform->set_rotation(glm::quat());
    } else if (camera_rig_type_ == ORBIT_PIVOT) {
        glm::vec3 pivot(getVec3("pivot"));
        transform->set_position(pivot.x, pivot.y,
                pivot.z + getFloat("distance"));
        transform->set_rotation(glm::quat());
        transform->rotateWithPivot(transform_rotation.w,
                transform_rotation.x, transform_rotation.y,
                transform_rotation.z, pivot.x, pivot.y, pivot.z);
    }
}

void CameraRig::setHeadTransform(Transform *transform) {
    head_transform_ = transform;
}

Transform* CameraRig::getHeadTransform() const {
    return head_transform_;
}

glm::vec3 CameraRig::getLookAt() const {
    glm::mat4 model_matrix = getHeadTransform()->getModelMatrix();
    float x0 = model_matrix[3][0];
    float y0 = model_matrix[3][1];
    float z0 = model_matrix[3][2];
    float reciprocalW0 = 1 / model_matrix[3][3];
    x0 *= reciprocalW0;
    y0 *= reciprocalW0;
    z0 *= reciprocalW0;

    float x1 = model_matrix[2][0] * -1.0f + model_matrix[3][0];
    float y1 = model_matrix[2][1] * -1.0f + model_matrix[3][1];
    float z1 = model_matrix[2][2] * -1.0f + model_matrix[3][2];
    float reciprocalW1 = 1.0f
            / (model_matrix[2][3] * -1.0f + model_matrix[3][3]);
    x1 *= reciprocalW1;
    y1 *= reciprocalW1;
    z1 *= reciprocalW1;

    float lookAtX = x1 - x0;
    float lookAtY = y1 - y0;
    float lookAtZ = z1 - z0;
    float reciprocalLength = 1.0f
            / sqrtf(lookAtX * lookAtX + lookAtY * lookAtY + lookAtZ * lookAtZ);
    lookAtX *= reciprocalLength;
    lookAtY *= reciprocalLength;
    lookAtZ *= reciprocalLength;

    return glm::vec3(lookAtX, lookAtY, lookAtZ);
}

const glm::quat& CameraRig::getComplementaryRotation() const {
    return complementary_rotation_;
}

void CameraRig::setComplementaryRotation(const glm::quat& q) {
    complementary_rotation_ = q;
}

}
