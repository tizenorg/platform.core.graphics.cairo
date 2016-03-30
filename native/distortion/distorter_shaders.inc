// IMPORTANT NOTE: I Don't use #ifdef DISTORTER_SHADERS intentionally.
// The reason is that I want to include it twice: once with FIX_CHROMATIC_ABERRATION
// and once without.
// This file will be included only from a cpp file, so is no risk of including it
// more than the 2 times it's included intentionally.

namespace gvr {

// Vertex shader

#ifdef FIX_CHROMATIC_ABERRATION
const char vertex_shader_chromatic_aberration[] =
#else
const char vertex_shader_no_chromatic_aberration[] =
#endif

        "uniform mat3 u_transformation;\n"
        "attribute vec4 a_position;\n"
        "attribute vec2 a_tex_coord;\n"
        "varying vec2 v_uv_g;\n"

#ifdef FIX_CHROMATIC_ABERRATION
        "varying vec2 v_uv_r;\n"
        "varying vec2 v_uv_b;\n"
#endif

        // warp the image to a new point of view
        "vec2 warpVertex(in vec2 vertex) {\n"
             "vec3 before3D = vec3(vertex.x, vertex.y, 1.0);\n"
             "vec3 after3D = u_transformation * before3D;\n"
             "return vec2(after3D.x/after3D.z, after3D.y/after3D.z);\n"
         "}\n"

        "void main() {\n"

#ifdef FIX_CHROMATIC_ABERRATION

        "vec2 image_center = vec2(0.5, 0.5);\n"

        // scale red and blue channels around image center to fix chromatic aberration
        // Note: I could do this step only once in pre-processing, but:
        // * then we have 3 attributes which Vittal says can be a bottle neck
        // * we will have 3 texture coordinates in mesh.h (like I did earlier) which makes Mesh more difficult to maintain
        // * Since currently we have only 441 vertices (and probably not much more ever), then the overhead of this calculation
        //   here is very minor.
        "vec2 red_tex_coord = (a_tex_coord-image_center)*0.992 + image_center;\n"
        "vec2 blue_tex_coord = (a_tex_coord-image_center)*1.01 + image_center;\n"
#endif

        "v_uv_g = warpVertex(a_tex_coord);\n"

#ifdef FIX_CHROMATIC_ABERRATION
        "v_uv_r = warpVertex(red_tex_coord);\n"
        "v_uv_b = warpVertex(blue_tex_coord);\n"
#endif

        "gl_Position = a_position;\n"

        "}\n";


//==================================================================//


// Fragmant shader

#ifdef FIX_CHROMATIC_ABERRATION
const char fragment_shader_chromatic_aberration[] =
#else
const char fragment_shader_no_chromatic_aberration[] =
#endif

        "precision highp float;\n"
        "uniform sampler2D u_texture;\n"
        "varying vec2 v_uv_g;\n"

#ifdef FIX_CHROMATIC_ABERRATION
        "varying vec2 v_uv_r;\n"
        "varying vec2 v_uv_b;\n"
#endif

        "void main() {\n"

#ifdef FIX_CHROMATIC_ABERRATION
        // sample for each color channels separately from different location
        // in the texture to compensate for chromatic aberration
        "float r = texture2D(u_texture, v_uv_r).r;\n"
        "float g = texture2D(u_texture, v_uv_g).g;\n"
        "float b = texture2D(u_texture, v_uv_b).b;\n"
        "gl_FragColor = vec4(r, g, b, 1.0);\n"
#else
        "gl_FragColor = texture2D(u_texture, v_uv_g);\n"
#endif
        "}\n";

}
