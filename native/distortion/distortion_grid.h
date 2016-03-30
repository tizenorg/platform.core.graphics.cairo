/***************************************************************************
 * Mesh for distorting the scene.
 ***************************************************************************/

#ifndef DISTORTION_GRID_H_
#define DISTORTION_GRID_H_

#include "GLES3/gl3.h"
#include "glm/glm.hpp"

#include "objects/hybrid_object.h"
#include "objects/mesh.h"

namespace gvr {
class DistortionData;
class EngineData;

class DistortionGrid: public HybridObject {
public:
    DistortionGrid();
    ~DistortionGrid();

    void update(bool is_distortion, float freeParam1, float freeParam2); // free Params are for debug purpose

    void forceShouldReset();

    Mesh& mesh() {
        return mesh_;
    }

private:
    DistortionGrid(const DistortionGrid& distortion_grid);
    DistortionGrid(DistortionGrid&& distortion_grid);
    DistortionGrid& operator=(const DistortionGrid& distortion_grid);
    DistortionGrid& operator=(DistortionGrid&& distortion_grid);

private:
    Mesh mesh_;
};

}
#endif
