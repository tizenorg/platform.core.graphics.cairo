/***************************************************************************
 * Mesh for distorting the scene.
 ***************************************************************************/

#include "distortion_grid.h"
#include "distorted_grid_loader.h"
#include "util/gvr_log.h"

namespace gvr {

/**
 * Must be called by the main gl thread so the mesh can establish
 * its proper "affinity".
 */
DistortionGrid::DistortionGrid() :
        HybridObject(), mesh_() {
    mesh_.obtainDeleter();
}

DistortionGrid::~DistortionGrid() {
}


// build a grid to fix distortion correction
// The parameters:
// freeParam1, freeParam2 - free parameters for debugging purpose
void DistortionGrid::update(bool is_distortion, float freeParam1, float freeParam2) {
    std::vector<glm::vec3> distortedVertices;
    DistortedGridLoader::loadVertices(distortedVertices, int(freeParam1));

    int distortion_grid_size = int(sqrt(distortedVertices.size()));

    // aligned texture coordinates (range 0 to 1)
    std::vector<glm::vec2> tex_coords;
    for (int j = distortion_grid_size - 1; j >=0; --j) {
        float y = j / float(distortion_grid_size-1);
        for (int i = 0; i < distortion_grid_size; ++i) {
            float x = i / float(distortion_grid_size-1);
            tex_coords.push_back(glm::vec2(x, y));
        }
    }

    if (!is_distortion) {
        // This code can be used to test a version without distortion for debug purpose.
        distortedVertices.clear();
        for (int j = distortion_grid_size - 1; j >= 0; --j) {
            float y = j / float(distortion_grid_size - 1);
            for (int i = 0; i < distortion_grid_size; ++i) {
                float x = i / float(distortion_grid_size - 1);
                distortedVertices.push_back(
                        glm::vec3(2 * x - 1, 2 * y - 1, 0.0f));
            }
        }
    }

    mesh_.set_vertices(distortedVertices);
    mesh_.set_tex_coords(tex_coords);

    std::vector<unsigned short> triangles;

    for (int j = 0; j < distortion_grid_size-1; ++j) {
        for (int i = 0; i < distortion_grid_size-1; ++i) {
            triangles.push_back((i) + (j) * distortion_grid_size);
            triangles.push_back((i + 1) + (j) * distortion_grid_size);
            triangles.push_back((i) + (j + 1) * distortion_grid_size);
            triangles.push_back((i + 1) + (j) * distortion_grid_size);
            triangles.push_back((i + 1) + (j + 1) * distortion_grid_size);
            triangles.push_back((i) + (j + 1) * distortion_grid_size);
        }
    }
    mesh_.set_triangles(triangles);
}

void DistortionGrid::forceShouldReset() {
    LOGW("DistortionGrid: forceShouldReset  \n");
    mesh_.forceShouldReset();
}

}
