/* Cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Eric Anholt
 * Copyright © 2009 Chris Wilson
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is Eric Anholt.
 */

/*
 * cairo-gl.h:
 *
 * The cairo-gl backend provides an implementation of possibly
 * hardware-accelerated cairo rendering by targeting the OpenGL API.
 * The goal of the cairo-gl backend is to provide better performance
 * with equal functionality to cairo-image where possible.  It does
 * not directly provide for applying additional OpenGL effects to
 * cairo surfaces.
 *
 * Cairo-gl allows interoperability with other GL rendering through GL
 * context sharing.  Cairo-gl surfaces are created in reference to a
 * #cairo_device_t, which represents a GL context created by the user.
 * When that GL context is created with its sharePtr set to another
 * context (or vice versa), its objects (textures backing cairo-gl
 * surfaces) can be accessed in the other OpenGL context.  This allows
 * cairo-gl to maintain its drawing state in one context while the
 * user's 3D rendering occurs in the user's other context.
 *
 * However, as only one context can be current to a thread at a time,
 * cairo-gl may make its context current to the thread on any cairo
 * call which interacts with a cairo-gl surface or the cairo-gl
 * device.  As a result, the user must make their own context current
 * between any cairo calls and their own OpenGL rendering.
 **/

#ifndef CAIRO_GL_H
#define CAIRO_GL_H

#include "cairo.h"

#if CAIRO_HAS_GL_SURFACE || CAIRO_HAS_GLESV2_SURFACE || CAIRO_HAS_GLESV3_SURFACE || CAIRO_HAS_EVASGL_SURFACE

CAIRO_BEGIN_DECLS

cairo_public cairo_surface_t *
cairo_gl_surface_create (cairo_device_t *device,
			 cairo_content_t content,
			 int width, int height);

cairo_public cairo_surface_t *
cairo_gl_surface_create_for_texture (cairo_device_t *abstract_device,
				     cairo_content_t content,
				     unsigned int tex,
                                     int width, int height);
cairo_public void
cairo_gl_surface_set_size (cairo_surface_t *surface, int width, int height);

cairo_public int
cairo_gl_surface_get_width (cairo_surface_t *abstract_surface);

cairo_public int
cairo_gl_surface_get_height (cairo_surface_t *abstract_surface);

cairo_public void
cairo_gl_surface_swapbuffers (cairo_surface_t *surface);

cairo_public void
cairo_gl_device_set_thread_aware (cairo_device_t	*device,
				  cairo_bool_t		 thread_aware);

cairo_public cairo_status_t
cairo_gl_surface_set_binding_texture (cairo_surface_t *abstract_surface,
				      unsigned int    texture);

#if CAIRO_HAS_GLX_FUNCTIONS
#include <GL/glx.h>

cairo_public cairo_device_t *
cairo_glx_device_create (Display *dpy, GLXContext gl_ctx);

cairo_public Display *
cairo_glx_device_get_display (cairo_device_t *device);

cairo_public GLXContext
cairo_glx_device_get_context (cairo_device_t *device);

cairo_public cairo_surface_t *
cairo_gl_surface_create_for_window (cairo_device_t *device,
				    Window win,
				    int width, int height);
#endif

#if CAIRO_HAS_WGL_FUNCTIONS
#include <windows.h>

cairo_public cairo_device_t *
cairo_wgl_device_create (HGLRC rc);

cairo_public HGLRC
cairo_wgl_device_get_context (cairo_device_t *device);

cairo_public cairo_surface_t *
cairo_gl_surface_create_for_dc (cairo_device_t		*device,
				HDC			 dc,
				int			 width,
				int			 height);
#endif

#if CAIRO_HAS_CGL_FUNCTIONS
#include <OpenGL/OpenGL.h>

cairo_public cairo_device_t *
cairo_cgl_device_create (CGLContextObj ctx);

cairo_public cairo_surface_t *
cairo_gl_surface_create_for_cgl (cairo_device_t *device,
				 int		 width,
				 int		 height);

cairo_public CGLContextObj
cairo_cgl_device_get_context (cairo_device_t *device);
#endif

#if CAIRO_HAS_EGL_FUNCTIONS
#include <EGL/egl.h>

cairo_public cairo_device_t *
cairo_egl_device_create (EGLDisplay dpy, EGLContext egl);

cairo_public cairo_surface_t *
cairo_gl_surface_create_for_egl (cairo_device_t	*device,
				 EGLSurface	 egl,
				 int		 width,
				 int		 height);

cairo_public EGLDisplay
cairo_egl_device_get_display (cairo_device_t *device);

cairo_public EGLSurface
cairo_egl_device_get_context (cairo_device_t *device);

#endif

#if CAIRO_HAS_EVASGL_SURFACE
#include <Evas_GL.h>

/* Evas_GL.h does not define GLchar */
typedef char GLchar;

#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER	0x8CA9
#endif

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER	0x8CA8
#endif

#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT		0x402
#endif

#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL	0x84F9
#endif

#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8	0x88F0
#endif

#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES		0x8D57
#endif

#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT	0x821A
#endif

cairo_public cairo_device_t *
cairo_evas_gl_device_create (Evas_GL		*evas_gl,
			     Evas_GL_Context	*evas_context);

cairo_public cairo_surface_t *
cairo_gl_surface_create_for_evas_gl (cairo_device_t	*device,
				     Evas_GL_Surface	*evas_surface,
				     Evas_GL_Config     *evas_config,
				     int		 width,
				     int		 height);

cairo_public Evas_GL *
cairo_evas_gl_device_get_gl (cairo_device_t *device);

cairo_public Evas_GL_Context *
cairo_evas_gl_device_get_context (cairo_device_t *device);
#endif

CAIRO_END_DECLS
#else  /* CAIRO_HAS_GL_SURFACE */
# error Cairo was not compiled with support for the GL backend
#endif /* CAIRO_HAS_GL_SURFACE */

#endif /* CAIRO_GL_H */
