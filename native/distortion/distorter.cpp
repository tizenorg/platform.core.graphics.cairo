/***************************************************************************
 * GL program for distorting a rendered scene.
 ***************************************************************************/

#include "distorter.h"

#include "distortion/distortion_grid.h"

// include the distorter shader file twice: once for chromatic aberration correction and once without it
#include "distortion/distorter_shaders.inc"
#define FIX_CHROMATIC_ABERRATION
#include "distortion/distorter_shaders.inc"

#include "gl/gl_program.h"
#include "util/gvr_gl.h"

#include "util/gvr_log.h"

namespace gvr {

// fov in radians
Distorter::Distorter(float fov) :
        chromatic_aberration_(false) {

    program_[0] = new GLProgram(vertex_shader_no_chromatic_aberration, fragment_shader_no_chromatic_aberration);
    program_[1] = new GLProgram(vertex_shader_chromatic_aberration, fragment_shader_chromatic_aberration);

    for (int i=0; i<2; ++i)
    {
        a_position_[i] = glGetAttribLocation(program_[i]->id(), "a_position");
        a_tex_coord_[i] = glGetAttribLocation(program_[i]->id(), "a_tex_coord");
        u_texture_[i] = glGetUniformLocation(program_[i]->id(), "u_texture");
        u_transformation_[i] = glGetUniformLocation(program_[i]->id(), "u_transformation");
    }

	float scale = 2*tan(fov/2);

	glm::mat3 scale_matrix(glm::vec3( scale,      0.0f,          0.0f),
			               glm::vec3( 0.0f,       scale,         0.0f),
			               glm::vec3( 0.0f,       0.0f,          1.0f));

	glm::mat3 translation_matrix(glm::vec3( 1.0f,       0.0f,          -0.5f),
					             glm::vec3( 0.0f,       1.0f,          -0.5f),
					             glm::vec3( 0.0f,       0.0f,          1.0f));

	// Note: in glm::mat3 translation_matrix*scale_matrix actually calculates what I would expect
	// to be calculated by scale_matrix*translation_matrix. Very weird...
	internal_camera_matrix_ = translation_matrix*scale_matrix;

    transformation_matrix_ = glm::mat3(); // identity
}

Distorter::~Distorter() {
    delete program_[0];
    delete program_[1];
}

// calculate viewport for rendering
// Parameters:
// render_diameter_meters - the diameter of the area we want to render for each eye
// real_screen_width_meters - the width of the part of the screen per eye (in meters)
// real_screen_width_pixels - the width of the part of the screen per eye (in pixels)
// real_screen_height_pixels - the height of the part of the screen per eye (in pixels)
// shift_screen_center_meters - optional shift of the whole rendering area (in phones
//                              in which the center of the screen is not aligned with
//                              the center of the GearVR headset.
// lenses_IPD_meters - distance between centers of lenses
void Distorter::calculateViewport(float render_diameter_meters,
        float real_screen_width_meters, float real_screen_width_pixels,
        float real_screen_height_pixels, float shift_screen_center_meters,
        float lenses_IPD_meters) {

    // set the viewport location and dimensions

    // scale viewport to a specific size
    float scale = render_diameter_meters / real_screen_width_meters;

    // set a square viewport
    // ASSUMPTION: vertical and horizontal DPI are identical (valid assumption for all new phones)
    viewport_width_ = (int) (real_screen_width_pixels * scale);
    viewport_height_ = viewport_width_;

    //shift the rendering area if the screen center is not aligned with the VR center
    int shift_screen_center_pixels = (int) (shift_screen_center_meters
            * real_screen_width_pixels / real_screen_width_meters);

    // calculate the offset from the center of the render area of an eye-view to the center of the lens in pixels
    int lens_offset_pixels = (int) ((real_screen_width_meters * 0.5f
            - lenses_IPD_meters * 0.5f) * real_screen_width_pixels
            / real_screen_width_meters);

    int left_viewport_center = real_screen_width_pixels / 2 + lens_offset_pixels
            + shift_screen_center_pixels;
    int right_viewport_center = real_screen_width_pixels * 3 / 2 - lens_offset_pixels
            + shift_screen_center_pixels;

    // it's ok if viewport start at negative position
    left_viewport_x_ = left_viewport_center - viewport_width_ / 2;
    right_viewport_x_ = right_viewport_center - viewport_width_ / 2;

    bottom_viewport_y_ = (real_screen_height_pixels / 2 - viewport_height_ / 2);

}

// calculate viewport for rendering in monoscopic mode
// Parameters:
// real_screen_width_pixels - the width of the whole screen in pixel size
// real_screen_height_pixels - the height of the whole screen in pixel size
void Distorter::calculateViewportMono(float real_screen_width_pixels,
        float real_screen_height_pixels) {
    viewport_width_ = real_screen_width_pixels;
    viewport_height_ = real_screen_height_pixels;
    left_viewport_x_ = 0;
    bottom_viewport_y_ = 0;

}


void Distorter::setTimewarpData(float* pose_predicted, float* pose_draw, bool use_timewarp) {
    
    // if no time warp, use identity to warp.
    // I could use a uniform to instruct the shader not to warp (instead of using identity matrix),
    // but this would require an 'if' statement in the shader which will create more overhead than
    // the overhead of multiplying a vector by a matrix.
    if (!use_timewarp)
    {
        transformation_matrix_ = glm::mat3(); // identity
        return;
    }

    // orientation from where the scene were captured from (source)
    glm::quat pose_draw_quat = glm::quat(pose_draw[3], pose_draw[0], pose_draw[1], -pose_draw[2]);

    // orientation where the scene has to be warped to (target)
    glm::quat pose_predicted_quat = glm::quat(pose_predicted[3], pose_predicted[0], pose_predicted[1], -pose_predicted[2]);

    // difference quaternion
    // we want to know for each point in the target texture, where does it come from the source texture
    //glm::quat diff_rotation =  pose_draw_quat* glm::inverse(pose_predicted_quat);
    glm::quat diff_rotation =  pose_predicted_quat* glm::inverse(pose_draw_quat);

    if (false) {
        LOGE("Distorter::Show  %f %f %f %f    Draw: %f %f %f %f   Diff: %f %f %f %f \n",
                pose_predicted[3], pose_predicted[0], pose_predicted[1], pose_predicted[2],
                pose_draw[3], pose_draw[0], pose_draw[1], pose_draw[2],
            diff_rotation.w, diff_rotation.x, diff_rotation.y, diff_rotation.z);
    }

    // if the motion between draw and show orientations is very small (usually because our body shakes), don't warp
    // (sometimes we get nan when the motion is very small because the diff_rotation is invalid quaternion. We could get the
    //  angle by normalizing the quaternions, but we don't do it because we don't need the value of the tiny diff angle).
    // The threshold 0.001 was decided by looking at the data of a steady head versus a moving one.
    float diffAngle = glm::angle(diff_rotation);
    const float MIN_DIFF_ANGLE_RAD = 0.001f;
    if (std::isnan(diffAngle) || diffAngle < MIN_DIFF_ANGLE_RAD || diffAngle > (2*glm::pi<float>()-MIN_DIFF_ANGLE_RAD) ) {
        transformation_matrix_ = glm::mat3(); // identity
        return;
    }

    // difference in matrix
    glm::mat3 rotation_matrix = glm::mat3_cast(diff_rotation);

    // TODO: get rid of the transpose by changing the order of matrix multiplications
    transformation_matrix_ = glm::transpose(internal_camera_matrix_*rotation_matrix*glm::inverse(internal_camera_matrix_));

}

// Render one or two eyes
// (can call it once for the two eyes or separately for each eye)
// IMPORTANT: if render each eye separately, the left eye must be rendered first.
// Parameters:
// framebuffer - render texture that will render (used with unity plugin)
// left_texture_id - texture for left eye (set to -1 if don't want to draw this eye)
// right_texture_id - texture for right eye (set to -1 if don't want to draw this eye)
// distortion_grid - a grid which is used for sampling in a way that will fix the distortion
void Distorter::render(GLint framebuffer, GLuint left_texture_id, GLuint right_texture_id,
        DistortionGrid* distortion_grid, bool is_adreno, bool is_front_buffer) {

#if _GVRF_USE_GLES3_

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glDisable (GL_DEPTH_TEST);
    glDisable (GL_BLEND);
    glDisable (GL_CULL_FACE);

    distortion_grid->mesh().generateVAO();

    glUseProgram(program_[chromatic_aberration_]->id());

    glActiveTexture (GL_TEXTURE0);
    glUniform1i(u_texture_[chromatic_aberration_], 0);

    glBindVertexArray(
            distortion_grid->mesh().getVAOId(Material::DISTORTION_SHADER));


    int glError = glGetError();
    if (glError != 0) {  // == GL_INVALID_OPERATION, GL_NO_ERROR(0)
        LOGW("Distorter::render Error %d, and trying to reset GL...\n",
                glError);
        distortion_grid->mesh().forceShouldReset();

        // retry it
        distortion_grid->mesh().generateVAO();
        glBindVertexArray(
                distortion_grid->mesh().getVAOId(Material::DISTORTION_SHADER));
    }

    glUniformMatrix3fv(u_transformation_[chromatic_aberration_], 1 /*only setting 1 matrix*/, false /*transpose?*/,  glm::value_ptr(transformation_matrix_));

    // render left eye
    if (left_texture_id != -1) {

        glScissor(left_viewport_x_, bottom_viewport_y_, viewport_width_,
                viewport_height_);

        if (is_adreno) {
            // Clear the buffer (without clearing, Adreno draws junk around the rendered area even with glScissor)
            glClearColor(0, 0, 0, 1);
            glClear (GL_COLOR_BUFFER_BIT);
        }

        glViewport(left_viewport_x_, bottom_viewport_y_, viewport_width_,
                viewport_height_);

        glBindTexture(GL_TEXTURE_2D, left_texture_id);
        glDrawElements(GL_TRIANGLES, distortion_grid->mesh().triangles().size(),
                GL_UNSIGNED_SHORT, 0);
    }

    // render right eye
    if (right_texture_id != -1) {

        glScissor(right_viewport_x_, bottom_viewport_y_, viewport_width_,
                viewport_height_);

        if (is_adreno && is_front_buffer) {
            // Clear the buffer (without clearing, Adreno draws junk around the rendered area even with glScissor)
            glClearColor(0, 0, 0, 1);
            glClear (GL_COLOR_BUFFER_BIT);
        }

        glViewport(right_viewport_x_, bottom_viewport_y_, viewport_width_,
                viewport_height_);

        glBindTexture(GL_TEXTURE_2D, right_texture_id);
        glDrawElements(GL_TRIANGLES, distortion_grid->mesh().triangles().size(),
                GL_UNSIGNED_SHORT, 0);
    }

    glBindVertexArray(0);

    checkGlError("Distorter::render()");

#else

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glDisable (GL_DEPTH_TEST);
    glDisable (GL_BLEND);
    glDisable (GL_CULL_FACE);

    glUseProgram(program_->id());

    glVertexAttribPointer(a_position_, 3, GL_FLOAT, GL_FALSE, 0,
            distortion_grid->mesh().vertices().data());
    glEnableVertexAttribArray(a_position_);

    // set different tex coordinates for each color channel to correct chromatic aberration
    glVertexAttribPointer(a_uv_, 2, GL_FLOAT, GL_FALSE, 0,
            distortion_grid->mesh().tex_coords_red().data());
    glEnableVertexAttribArray(a_uv_);

    glActiveTexture (GL_TEXTURE0);
    glUniform1i(u_texture_, 0);

    // TODO: is it ok that the last parameter of glDrawElements is
    // distortion_grid->mesh().triangles().data() for GLES2 and 0 for GLES3?

    // render left eye
    glViewport(left_viewport_x, viewport_y, viewport_w, viewport_h);
    glBindTexture(GL_TEXTURE_2D, left_texture_id);
    glDrawElements(GL_TRIANGLES, distortion_grid->mesh().triangles().size(),
            GL_UNSIGNED_SHORT, distortion_grid->mesh().triangles().data());

    // render right eye
    glViewport(right_viewport_x, viewport_y, viewport_w, viewport_h);
    glBindTexture(GL_TEXTURE_2D, right_texture_id);
    glDrawElements(GL_TRIANGLES, distortion_grid->mesh().triangles().size(),
            GL_UNSIGNED_SHORT, distortion_grid->mesh().triangles().data());

    checkGlError("Distorter::render()");
#endif
}
}
