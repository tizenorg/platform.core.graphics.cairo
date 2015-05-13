/* Cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Chris Wilson
 * Copyright © 2015 Samsung Research America Inc - Silicon Valley
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
 * The Initial Developer of the Original Code is Chris Wilson.
 */

#include "cairo-boilerplate-private.h"

#include <cairo-gl.h>

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

static const cairo_user_data_key_t gl_closure_key;

typedef struct _cgl_target_closure {
    CGLContextObj context;
    cairo_device_t *device;
    cairo_surface_t *surface;
} cgl_target_closure_t;

static void
_cairo_boilerplate_cgl_cleanup (void *closure)
{
    cgl_target_closure_t *gltc = closure;

    cairo_device_finish (gltc->device);
    cairo_device_destroy (gltc->device);

    CGLSetCurrentContext (NULL);
    CGLDestroyContext (gltc->context);

    free (gltc);
}

static cairo_surface_t *
_cairo_boilerplate_cgl_create_surface (const char		 *name,
				       cairo_content_t		  content,
				       double			  width,
				       double			  height,
				       double			  max_width,
				       double			  max_height,
				       cairo_boilerplate_mode_t   mode,
				       void			**closure)
{
    cgl_target_closure_t *gltc;
    cairo_surface_t *surface;
    CGLPixelFormatObj pixelformat;
    CGLContextObj context;
    GLint npix;
    CGLError error;

    CGLPixelFormatAttribute attribs[] = {
	kCGLPFAAlphaSize, 8,
	kCGLPFAColorSize, 24,
	kCGLPFAOpenGLProfile, kCGLOGLPVersion_3_2_Core,
	kCGLPFAAccelerated,
	0
    };

    error = CGLChoosePixelFormat (attribs, &pixelformat, &npix);
    if (error != kCGLNoError || ! pixelformat)
	return NULL;

    error = CGLCreateContext (pixelformat, NULL, &context);
    if (error != kCGLNoError) {
	CGLReleasePixelFormat (pixelformat);
	return NULL;
    }

    CGLReleasePixelFormat (pixelformat);

    gltc = xcalloc (1, sizeof (cgl_target_closure_t));
    *closure = gltc;
    gltc->context = context;

    gltc->device = cairo_cgl_device_create (gltc->context);

    if (width < 1)
	width = 1;
    if (height < 1)
	height = 1;

    gltc->surface = surface = cairo_gl_surface_create (gltc->device,
						       content,
						       ceil (width),
						       ceil (height));
    if (cairo_surface_status (surface))
	_cairo_boilerplate_cgl_cleanup (gltc);

    return surface;
}

static void
_cairo_boilerplate_cgl_synchronize (void *closure)
{
    cgl_target_closure_t *gltc = closure;

    if (cairo_device_acquire (gltc->device))
	return;

    glFinish ();

    cairo_device_release (gltc->device);
}

static const cairo_boilerplate_target_t targets[] = {
    {
	"cgl", "gl", NULL, NULL,
	CAIRO_SURFACE_TYPE_GL, CAIRO_CONTENT_COLOR_ALPHA, 1,
	"cairo_cgl_device_create",
	_cairo_boilerplate_cgl_create_surface,
	cairo_surface_create_similar,
	NULL, NULL,
	_cairo_boilerplate_get_image_surface,
	cairo_surface_write_to_png,
	_cairo_boilerplate_cgl_cleanup,
	_cairo_boilerplate_cgl_synchronize,
        NULL,
	TRUE, FALSE, FALSE
    }
};
CAIRO_BOILERPLATE (egl, targets)
