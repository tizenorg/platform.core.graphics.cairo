/***************************************************************************
 * GL program for distorting a rendered scene.
 ***************************************************************************/

#ifndef DISTORTER_H_
#define DISTORTER_H_

#include <memory>

#include "GLES3/gl3.h"
#include "glm/gtc/type_ptr.hpp"
#include "objects/hybrid_object.h"

namespace gvr {

class DistortionGrid;
class GLProgram;

class Distorter: public HybridObject {
public:
    // fov in radians
    Distorter(float fov);
    virtual ~Distorter();
    void recycle();
    void setTimewarpData(float* pose_predicted, float* pose_draw, bool use_timewarp);
    void render(GLuint left_texture_id, GLuint right_texture_id,
            DistortionGrid* distortion_grid, bool isAdreno = false,
            bool isFrontBuffer = false);
    void render(GLint framebuffer, GLuint left_texture_id, GLuint right_texture_id,
            DistortionGrid* distortion_grid, bool is_adreno = false,
            bool is_front_buffer = false);

    void setChromaticAberrationMode(bool chromaticAberration) { chromatic_aberration_ = chromaticAberration; }

    // calculate viewport to display the rendering result
    void calculateViewport(float desiredDisplayWidthMeters,
            float realScreenWidthMeters, float realScreenWidthPixels,
            float realScreenHeightPixels, float shiftScreenCenterMeters,
            float lensesIPDMeters);

    void calculateViewportMono(float real_screen_width_pixels,
            float real_screen_height_pixels);

    int getLeftViewportX() const { return left_viewport_x_; }
    int getRightViewportX() const { return right_viewport_x_; }
    int getBottomViewportY() const { return bottom_viewport_y_; }
    int getViewportWidth() const { return viewport_width_; }
    int getViewportHeight() const { return viewport_height_; }

private:
    Distorter(const Distorter& distorter);
    Distorter(Distorter&& distorter);
    Distorter& operator=(const Distorter& distorter);
    Distorter& operator=(Distorter&& distorter);

    void calculateImageCenterAfterWarping();

private:
    GLProgram* program_[2];
    GLuint a_position_[2];
    GLuint a_tex_coord_[2];
    GLuint u_texture_[2];

    GLuint u_transformation_[2];

    bool chromatic_aberration_;

    glm::mat3 transformation_matrix_;

    glm::mat3 internal_camera_matrix_;

    // Viewport to render distortion correction
    int left_viewport_x_ = 0, right_viewport_x_ = 0, bottom_viewport_y_ = 0,
            viewport_width_ = 0, viewport_height_ = 0;
};

}
#endif
