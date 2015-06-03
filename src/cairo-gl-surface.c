/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Eric Anholt
 * Copyright © 2009 Chris Wilson
 * Copyright © 2005,2010 Red Hat, Inc
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
 * The Initial Developer of the Original Code is Red Hat, Inc.
 *
 * Contributor(s):
 *	Benjamin Otte <otte@gnome.org>
 *	Carl Worth <cworth@cworth.org>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 *	Eric Anholt <eric@anholt.net>
 */

#include "cairoint.h"

#include "cairo-gl-private.h"

#include "cairo-composite-rectangles-private.h"
#include "cairo-compositor-private.h"
#include "cairo-default-context-private.h"
#include "cairo-error-private.h"
#include "cairo-image-surface-inline.h"
#include "cairo-surface-backend-private.h"
#include "cairo-surface-shadow-private.h"
#include "cairo-surface-scale-translate-private.h"

static const cairo_surface_backend_t _cairo_gl_surface_backend;

static cairo_status_t
_cairo_gl_surface_flush (void *abstract_surface, unsigned flags);

static cairo_bool_t _cairo_surface_is_gl (cairo_surface_t *surface)
{
    return surface->backend == &_cairo_gl_surface_backend;
}

static cairo_surface_t *
_cairo_gl_surface_shadow_surface (void *surface,
				  const cairo_bool_t has_blur,
				  int width, int height,
				  int *width_out, int *height_out)
{
    int shadow_width, shadow_height;
    cairo_gl_surface_t *shadow_surface = NULL;

    cairo_gl_surface_t *dst = (cairo_gl_surface_t *)surface;
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)dst->base.device;
    if (ctx == NULL)
	return NULL;

    shadow_surface = ctx->shadow_scratch_surfaces[0];

    if (shadow_surface) {
	shadow_width = shadow_surface->width;
	shadow_height = shadow_surface->height;

	if (! has_blur) {
	    if(shadow_width >= width &&
	       shadow_height >= height) {
		*width_out = width;
		*height_out = height;
		return cairo_surface_reference (&shadow_surface->base);
	    }
	    else {
		cairo_surface_destroy (&shadow_surface->base);
		shadow_surface = NULL;
	    }
	}
	else {
	    if (shadow_width * 2 < width &&
		shadow_height * 2 < height) {
		if (shadow_width < MAX_SCRATCH_SIZE ||
		    shadow_height < MAX_SCRATCH_SIZE) {
		    cairo_surface_destroy (&shadow_surface->base);
		    shadow_surface = NULL;
		}
	    }
	    else if (shadow_width > 4 * width &&
		     shadow_height > 4 * height) {
		cairo_surface_destroy (&shadow_surface->base);
		shadow_surface = NULL;
	    }
	}
    }

    if (! shadow_surface) {
	shadow_width = shadow_height = MIN_SCRATCH_SIZE;
	if (has_blur) {
	    while (shadow_width * 2 < width) {
		shadow_width *= 2;
		if (shadow_width == MAX_SCRATCH_SIZE)
		    break;
		else if (shadow_width > MAX_SCRATCH_SIZE) {
		    shadow_width *= 0.5;
		    break;
		}
	    }
	    while (shadow_height * 2 < height) {
		shadow_height *= 2;
		if (shadow_height == MAX_SCRATCH_SIZE)
		    break;
		else if (shadow_height > MAX_SCRATCH_SIZE) {
		    shadow_height *= 0.5;
		    break;
		}
	    }
	}
	else {
	    while (shadow_width < width) {
		shadow_width *= 2;
		if (shadow_width == MAX_SCRATCH_SIZE)
		    break;
		else if (shadow_width > MAX_SCRATCH_SIZE) {
		    shadow_width *= 0.5;
		    break;
		}
	    }
	    while (shadow_height < height) {
		shadow_height *= 2;
		if (shadow_height == MAX_SCRATCH_SIZE)
		    break;
		else if (shadow_height > MAX_SCRATCH_SIZE) {
		    shadow_height *= 0.5;
		    break;
		}
	    }
	}


	shadow_surface = (cairo_gl_surface_t *)
		_cairo_gl_surface_create_scratch (ctx,
						  CAIRO_CONTENT_COLOR_ALPHA,
						  shadow_width,
						  shadow_height);
	if (unlikely (shadow_surface->base.status)) {
	    cairo_surface_destroy (&shadow_surface->base);
	    return NULL;
	}

	_cairo_surface_release_device_reference (&shadow_surface->base);
    }

    ctx->shadow_scratch_surfaces[0] = shadow_surface;

    shadow_surface->needs_to_cache = FALSE;
    shadow_surface->force_no_cache = TRUE;

    *width_out = width;
    *height_out = height;

    if (has_blur) {
	while (*width_out > shadow_width) {
	    *width_out *= 0.5;
	}

	while (*height_out > shadow_height) {
	    *height_out *= 0.5;
	}
    }
    else {
	if (*width_out > MAX_SCRATCH_SIZE)
	    *width_out *= 0.5;
	if (*height_out > MAX_SCRATCH_SIZE)
	    *height_out *= 0.5;
    }

    return cairo_surface_reference (&shadow_surface->base);
}

static cairo_surface_t *
_cairo_gl_surface_shadow_mask_surface (void *surface,
				       int width, int height,
				       unsigned int index)
{
    cairo_gl_surface_t *mask_surface = NULL;

    cairo_gl_surface_t *dst = (cairo_gl_surface_t *)surface;
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)dst->base.device;
    if (ctx == NULL)
	return NULL;

    if (index > 1)
	return NULL;

    mask_surface = ctx->shadow_masks[index];

    if (mask_surface) {
	if (mask_surface->width != width ||
	    mask_surface->height != height) {
	    cairo_surface_destroy (&mask_surface->base);
	    mask_surface = NULL;
	    ctx->shadow_masks[index] = NULL;
	}
    }

    if (! mask_surface) {
	mask_surface = (cairo_gl_surface_t *)
		_cairo_gl_surface_create_scratch (ctx,
						  CAIRO_CONTENT_COLOR_ALPHA,
						  width,
						  height);
	if (unlikely (mask_surface->base.status)) {
	    cairo_surface_destroy (&mask_surface->base);
	    return NULL;
	}
	_cairo_surface_release_device_reference (&mask_surface->base);
    }

    ctx->shadow_masks[index] = mask_surface;

    mask_surface->needs_to_cache = FALSE;
    mask_surface->force_no_cache = TRUE;

    return cairo_surface_reference (&mask_surface->base);
}

static cairo_surface_t *
_cairo_gl_surface_glyph_shadow_surface (void *surface,
					int width, int height,
					cairo_bool_t for_source)
{
    int shadow_width, shadow_height;
    cairo_gl_surface_t *shadow_surface = NULL;

    cairo_gl_surface_t *dst = (cairo_gl_surface_t *)surface;
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)dst->base.device;
    if (ctx == NULL)
	return NULL;

    if (! for_source)
	shadow_surface = ctx->shadow_scratch_surfaces[1];
    else
	shadow_surface = ctx->shadow_scratch_surfaces[2];

    if (shadow_surface) {
	shadow_width = shadow_surface->width;
	shadow_height = shadow_surface->height;

	if (shadow_width < width ||
	    shadow_height < height) {
	   cairo_surface_destroy (&shadow_surface->base);
	   shadow_surface = NULL;
	}
    }

    if (! shadow_surface) {
	shadow_surface = (cairo_gl_surface_t *)
		_cairo_gl_surface_create_scratch (ctx,
						  CAIRO_CONTENT_COLOR_ALPHA,
						  width, height);
	if (unlikely (shadow_surface->base.status)) {
	    cairo_surface_destroy (&shadow_surface->base);
	    return NULL;
	}
	_cairo_surface_release_device_reference (&shadow_surface->base);
    }

    if (! for_source)
	ctx->shadow_scratch_surfaces[1] = shadow_surface;
    else
	ctx->shadow_scratch_surfaces[2] = shadow_surface;

    shadow_surface->needs_to_cache = FALSE;
    shadow_surface->force_no_cache = TRUE;

    return cairo_surface_reference (&shadow_surface->base);
}

static cairo_surface_t *
_cairo_gl_surface_glyph_shadow_mask_surface (void *surface,
					     int width, int height,
					     unsigned index)
{
    cairo_gl_surface_t *mask_surface = NULL;

    cairo_gl_surface_t *dst = (cairo_gl_surface_t *)surface;
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)dst->base.device;
    if (ctx == NULL)
	return NULL;

    if (index > 1)
	return NULL;

    mask_surface = ctx->shadow_masks[index + 2];

    if (mask_surface) {
	if (mask_surface->width != width ||
	    mask_surface->height != height) {
	    cairo_surface_destroy (&mask_surface->base);
	    mask_surface = NULL;
	    ctx->shadow_masks[index + 2] = NULL;
	}
    }

    if (! mask_surface) {
	mask_surface = (cairo_gl_surface_t *)
		_cairo_gl_surface_create_scratch (ctx,
						  CAIRO_CONTENT_ALPHA,
						  width,
						  height);
	if (unlikely (mask_surface->base.status)) {
	    cairo_surface_destroy (&mask_surface->base);
	    return NULL;
	}
	_cairo_surface_release_device_reference (&mask_surface->base);
    }

    ctx->shadow_masks[index + 2] = mask_surface;

    mask_surface->needs_to_cache = FALSE;
    mask_surface->force_no_cache = TRUE;

    return cairo_surface_reference (&mask_surface->base);
}

static cairo_bool_t
_cairo_gl_get_image_format_and_type_gles2 (pixman_format_code_t pixman_format,
					   GLenum *internal_format, GLenum *format,
					   GLenum *type, cairo_bool_t *has_alpha,
					   cairo_bool_t *needs_swap)
{
    cairo_bool_t is_little_endian = _cairo_is_little_endian ();

    *has_alpha = TRUE;

    switch ((int) pixman_format) {
    case PIXMAN_a8r8g8b8:
	*internal_format = GL_BGRA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = !is_little_endian;
	return TRUE;

    case PIXMAN_x8r8g8b8:
	*internal_format = GL_BGRA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_BYTE;
	*has_alpha = FALSE;
	*needs_swap = !is_little_endian;
	return TRUE;

    case PIXMAN_a8b8g8r8:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = !is_little_endian;
	return TRUE;

    case PIXMAN_x8b8g8r8:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_BYTE;
	*has_alpha = FALSE;
	*needs_swap = !is_little_endian;
	return TRUE;

    case PIXMAN_b8g8r8a8:
	*internal_format = GL_BGRA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = is_little_endian;
	return TRUE;

    case PIXMAN_b8g8r8x8:
	*internal_format = GL_BGRA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_BYTE;
	*has_alpha = FALSE;
	*needs_swap = is_little_endian;
	return TRUE;

    case PIXMAN_r8g8b8:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = is_little_endian;
	return TRUE;

    case PIXMAN_b8g8r8:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = !is_little_endian;
	return TRUE;

    case PIXMAN_r5g6b5:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_SHORT_5_6_5;
	*needs_swap = FALSE;
	return TRUE;

    case PIXMAN_b5g6r5:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_SHORT_5_6_5;
	*needs_swap = TRUE;
	return TRUE;

    case PIXMAN_a1b5g5r5:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_SHORT_5_5_5_1;
	*needs_swap = TRUE;
	return TRUE;

    case PIXMAN_x1b5g5r5:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_SHORT_5_5_5_1;
	*has_alpha = FALSE;
	*needs_swap = TRUE;
	return TRUE;

    case PIXMAN_a8:
	*internal_format = GL_ALPHA;
	*format = GL_ALPHA;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = FALSE;
	return TRUE;

    default:
	return FALSE;
    }
}

static cairo_bool_t
_cairo_gl_get_image_format_and_type_gl (pixman_format_code_t pixman_format,
					GLenum *internal_format, GLenum *format,
					GLenum *type, cairo_bool_t *has_alpha,
					cairo_bool_t *needs_swap)
{
    *has_alpha = TRUE;
    *needs_swap = FALSE;

    switch (pixman_format) {
    case PIXMAN_a8r8g8b8:
	*internal_format = GL_RGBA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_INT_8_8_8_8_REV;
	return TRUE;
    case PIXMAN_x8r8g8b8:
	*internal_format = GL_RGB;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_INT_8_8_8_8_REV;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_a8b8g8r8:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_INT_8_8_8_8_REV;
	return TRUE;
    case PIXMAN_x8b8g8r8:
	*internal_format = GL_RGB;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_INT_8_8_8_8_REV;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_b8g8r8a8:
	*internal_format = GL_RGBA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_INT_8_8_8_8;
	return TRUE;
    case PIXMAN_b8g8r8x8:
	*internal_format = GL_RGB;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_INT_8_8_8_8;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_r8g8b8:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_BYTE;
	return TRUE;
    case PIXMAN_b8g8r8:
	*internal_format = GL_RGB;
	*format = GL_BGR;
	*type = GL_UNSIGNED_BYTE;
	return TRUE;
    case PIXMAN_r5g6b5:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_SHORT_5_6_5;
	return TRUE;
    case PIXMAN_b5g6r5:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_SHORT_5_6_5_REV;
	return TRUE;
    case PIXMAN_a1r5g5b5:
	*internal_format = GL_RGBA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	return TRUE;
    case PIXMAN_x1r5g5b5:
	*internal_format = GL_RGB;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_a1b5g5r5:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	return TRUE;
    case PIXMAN_x1b5g5r5:
	*internal_format = GL_RGB;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_a8:
	*internal_format = GL_ALPHA;
	*format = GL_ALPHA;
	*type = GL_UNSIGNED_BYTE;
	return TRUE;

    case PIXMAN_a2b10g10r10:
    case PIXMAN_x2b10g10r10:
    case PIXMAN_a4r4g4b4:
    case PIXMAN_x4r4g4b4:
    case PIXMAN_a4b4g4r4:
    case PIXMAN_x4b4g4r4:
    case PIXMAN_r3g3b2:
    case PIXMAN_b2g3r3:
    case PIXMAN_a2r2g2b2:
    case PIXMAN_a2b2g2r2:
    case PIXMAN_c8:
    case PIXMAN_x4a4:
    /* case PIXMAN_x4c4: */
    case PIXMAN_x4g4:
    case PIXMAN_a4:
    case PIXMAN_r1g2b1:
    case PIXMAN_b1g2r1:
    case PIXMAN_a1r1g1b1:
    case PIXMAN_a1b1g1r1:
    case PIXMAN_c4:
    case PIXMAN_g4:
    case PIXMAN_a1:
    case PIXMAN_g1:
    case PIXMAN_yuy2:
    case PIXMAN_yv12:
    case PIXMAN_x2r10g10b10:
    case PIXMAN_a2r10g10b10:
    case PIXMAN_r8g8b8x8:
    case PIXMAN_r8g8b8a8:
    case PIXMAN_x14r6g6b6:
    default:
	return FALSE;
    }
}

/*
 * Extracts pixel data from an image surface.
 */
static cairo_status_t
_cairo_gl_surface_extract_image_data (cairo_image_surface_t *image,
				      int x, int y,
				      int width, int height,
				      void **output)
{
    int cpp = PIXMAN_FORMAT_BPP (image->pixman_format) / 8;
    char *data = _cairo_malloc_ab (width * height, cpp);
    char *dst = data;
    unsigned char *src = image->data + y * image->stride + x * cpp;
    int i;

    if (unlikely (data == NULL))
	return CAIRO_STATUS_NO_MEMORY;

    for (i = 0; i < height; i++) {
	memcpy (dst, src, width * cpp);
	src += image->stride;
	dst += width * cpp;
    }

    *output = data;

    return CAIRO_STATUS_SUCCESS;
}

cairo_bool_t
_cairo_gl_get_image_format_and_type (cairo_gl_flavor_t flavor,
				     pixman_format_code_t pixman_format,
				     GLenum *internal_format, GLenum *format,
				     GLenum *type, cairo_bool_t *has_alpha,
				     cairo_bool_t *needs_swap)
{
    if (flavor == CAIRO_GL_FLAVOR_DESKTOP)
	return _cairo_gl_get_image_format_and_type_gl (pixman_format,
						       internal_format, format,
						       type, has_alpha,
						       needs_swap);
    else
	return _cairo_gl_get_image_format_and_type_gles2 (pixman_format,
							  internal_format, format,
							  type, has_alpha,
							  needs_swap);

}

cairo_bool_t
_cairo_gl_operator_is_supported (cairo_operator_t op)
{
    return op < CAIRO_OPERATOR_SATURATE;
}

static void
_cairo_gl_surface_embedded_operand_init (cairo_gl_surface_t *surface)
{
    cairo_gl_operand_t *operand = &surface->operand;
    cairo_surface_attributes_t *attributes = &operand->texture.attributes;

    memset (operand, 0, sizeof (cairo_gl_operand_t));

    operand->type = CAIRO_GL_OPERAND_TEXTURE;
    operand->texture.surface = surface;
    operand->texture.tex = surface->tex;
    operand->pass = 0;

    if (_cairo_gl_device_requires_power_of_two_textures (surface->base.device)) {
	cairo_matrix_init_identity (&attributes->matrix);
    } else {
	cairo_matrix_init_scale (&attributes->matrix,
				 1.0 / surface->width,
				 1.0 / surface->height);
    }

    attributes->extend = CAIRO_EXTEND_NONE;
    attributes->filter = CAIRO_FILTER_NEAREST;
}

void
_cairo_gl_surface_init (cairo_device_t *device,
			cairo_gl_surface_t *surface,
			cairo_content_t content,
			int width, int height)
{
    assert (width > 0 && height > 0);

    _cairo_surface_init (&surface->base,
			 &_cairo_gl_surface_backend,
			 device,
			 content);

    surface->width = width;
    surface->height = height;
    surface->needs_update = FALSE;
    surface->size_changed = FALSE;
    surface->needs_to_cache = FALSE;
    surface->image_node = NULL;
    surface->force_no_cache = FALSE;

    surface->image_content_scale_x = 1.0;
    surface->image_content_scale_y = 1.0;
    surface->blur_stage = CAIRO_GL_BLUR_STAGE_NONE;

    surface->clip_on_stencil_buffer = NULL;

    surface->content_synced = TRUE;
    surface->content_cleared = FALSE;

    _cairo_gl_surface_embedded_operand_init (surface);
}

static cairo_surface_t *
_cairo_gl_surface_create_scratch_for_texture (cairo_gl_context_t   *ctx,
					      cairo_content_t	    content,
					      GLuint		    tex,
					      int		    width,
					      int		    height,
					      cairo_bool_t set_tex_param)
{
    cairo_gl_surface_t *surface;

    assert (width <= ctx->max_framebuffer_size && height <= ctx->max_framebuffer_size);
    surface = calloc (1, sizeof (cairo_gl_surface_t));
    if (unlikely (surface == NULL))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));

    surface->tex = tex;
    _cairo_gl_surface_init (&ctx->base, surface, content, width, height);

    surface->supports_msaa = ctx->supports_msaa;
    surface->num_samples = ctx->num_samples;
    surface->supports_stencil = TRUE;

    /* Create the texture used to store the surface's data. */
    if (!set_tex_param) {
    _cairo_gl_context_activate (ctx, CAIRO_GL_TEX_TEMP);
    ctx->dispatch.BindTexture (ctx->tex_target, surface->tex);
    ctx->dispatch.TexParameteri (ctx->tex_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ctx->dispatch.TexParameteri (ctx->tex_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    return &surface->base;
}

static cairo_surface_t *
_create_scratch_internal (cairo_gl_context_t *ctx,
			  cairo_content_t content,
			  int width,
			  int height,
			  cairo_bool_t for_caching)
{
    cairo_gl_surface_t *surface;
    GLenum format;
    GLuint tex;

    ctx->dispatch.GenTextures (1, &tex);
    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_scratch_for_texture (ctx, content,
						      tex, width, height, FALSE);
    if (unlikely (surface->base.status))
	return &surface->base;

    surface->owns_tex = TRUE;

    /* adjust the texture size after setting our real extents */
    if (width < 1)
	width = 1;
    if (height < 1)
	height = 1;

    switch (content) {
    default:
	ASSERT_NOT_REACHED;
    case CAIRO_CONTENT_COLOR_ALPHA:
	if (ctx->can_read_bgra)
	    format = GL_BGRA;
	else
	    format = GL_RGBA;
	break;
    case CAIRO_CONTENT_ALPHA:
	/* When using GL_ALPHA, compositing doesn't work properly, but for
	 * caching surfaces, we are just uploading pixel data, so it isn't
	 * an issue. */
	if (for_caching)
	    format = GL_ALPHA;
	else
	    format = GL_RGBA;
	break;
    case CAIRO_CONTENT_COLOR:
	/* GL_RGB is almost what we want here -- sampling 1 alpha when
	 * texturing, using 1 as destination alpha factor in blending,
	 * etc.  However, when filtering with GL_CLAMP_TO_BORDER, the
	 * alpha channel of the border color will also be clamped to
	 * 1, when we actually want the border color we explicitly
	 * specified.  So, we have to store RGBA, and fill the alpha
	 * channel with 1 when blending.
	 */
	if (ctx->can_read_bgra)
	    format = GL_BGRA;
	else
	    format = GL_RGBA;
	break;
    }

    ctx->dispatch.TexImage2D (ctx->tex_target, 0, format,
			      width, height, 0,
		  format, GL_UNSIGNED_BYTE, NULL);

    return &surface->base;
}

cairo_surface_t *
_cairo_gl_surface_create_scratch (cairo_gl_context_t   *ctx,
				  cairo_content_t	content,
				  int			width,
				  int			height)
{
    return _create_scratch_internal (ctx, content, width, height, FALSE);
}

cairo_surface_t *
_cairo_gl_surface_create_scratch_for_caching (cairo_gl_context_t *ctx,
					      cairo_content_t content,
					      int width,
					      int height)
{
    return _create_scratch_internal (ctx, content, width, height, TRUE);
}

static cairo_status_t
_cairo_gl_surface_clear (cairo_gl_surface_t  *surface,
                         const cairo_color_t *color)
{
    cairo_gl_context_t *ctx;
    cairo_status_t status;
    double r, g, b, a;

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
	return status;

    if (ctx->current_target == surface)
	_cairo_gl_composite_flush (ctx);

    /* FIXME:  for glesv3 and glesv2 with ANGLE extension of multisample
       supports, it is much more expensive to paint texture back to
       multisample renderbuffer.  Therefore, instead of clear
       texture, we clear the renderbuffer.
       In case, the next draw render target is texture, it will
       blit renderbuffer back to texture */
    if (ctx->gl_flavor != CAIRO_GL_FLAVOR_DESKTOP)
	_cairo_gl_context_set_destination (ctx, surface, TRUE);
    else
	_cairo_gl_context_set_destination (ctx, surface, surface->msaa_active);
    if (surface->base.content & CAIRO_CONTENT_COLOR) {
        r = color->red   * color->alpha;
        g = color->green * color->alpha;
        b = color->blue  * color->alpha;
    } else {
        r = g = b = 0;
    }
    if (surface->base.content & CAIRO_CONTENT_ALPHA) {
        a = color->alpha;
    } else {
        a = 1.0;
    }

    _disable_scissor_buffer (ctx);
    if (ctx->states_cache.clear_red != r ||
	ctx->states_cache.clear_green != g ||
	ctx->states_cache.clear_blue != b ||
	ctx->states_cache.clear_alpha != a) {

	ctx->states_cache.clear_red = r;
	ctx->states_cache.clear_green = g;
	ctx->states_cache.clear_blue = b;
	ctx->states_cache.clear_alpha = a;

	ctx->dispatch.ClearColor (r, g, b, a);
    }

    /* optimize for mobile gl driver with deferred rendering */
    if (surface->clip_on_stencil_buffer ||
	ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP)
	ctx->dispatch.Clear (GL_COLOR_BUFFER_BIT);
    else {
		if (surface->clip_on_stencil_buffer) {
			_cairo_clip_destroy(surface->clip_on_stencil_buffer);
			surface->clip_on_stencil_buffer = NULL;
		}
		ctx->dispatch.Clear (GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    if (a == 0)
	surface->base.is_clear = TRUE;

    surface->content_changed = TRUE;
    surface->content_synced = FALSE;
    surface->content_cleared = TRUE;
    return _cairo_gl_context_release (ctx, status);
}

static cairo_surface_t *
_cairo_gl_surface_create_and_clear_scratch (cairo_gl_context_t *ctx,
					    cairo_content_t content,
					    int width,
					    int height)
{
    cairo_gl_surface_t *surface;
    cairo_int_status_t status;

    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_scratch (ctx, content, width, height);
    if (unlikely (surface->base.status))
	return &surface->base;

    /* Cairo surfaces start out initialized to transparent (black) */
    status = _cairo_gl_surface_clear (surface, CAIRO_COLOR_TRANSPARENT);
    if (unlikely (status)) {
	cairo_surface_destroy (&surface->base);
	return _cairo_surface_create_in_error (status);
    }

    return &surface->base;
}

cairo_surface_t *
cairo_gl_surface_create (cairo_device_t		*abstract_device,
			 cairo_content_t	 content,
			 int			 width,
			 int			 height)
{
    cairo_gl_context_t *ctx;
    cairo_gl_surface_t *surface;
    cairo_status_t status;

    if (! CAIRO_CONTENT_VALID (content))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_CONTENT));

    if (abstract_device == NULL)
	return _cairo_image_surface_create_with_content (content, width, height);

    if (abstract_device->status)
	return _cairo_surface_create_in_error (abstract_device->status);

    if (abstract_device->backend->type != CAIRO_DEVICE_TYPE_GL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH));

    status = _cairo_gl_context_acquire (abstract_device, &ctx);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);

    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_and_clear_scratch (ctx, content, width, height);
    if (unlikely (surface->base.status)) {
	status = _cairo_gl_context_release (ctx, surface->base.status);
	cairo_surface_destroy (&surface->base);
	return _cairo_surface_create_in_error (status);
    }

    status = _cairo_gl_context_release (ctx, status);
    if (unlikely (status)) {
	cairo_surface_destroy (&surface->base);
	return _cairo_surface_create_in_error (status);
    }

    return &surface->base;
}
slim_hidden_def (cairo_gl_surface_create);

/**
 * cairo_gl_surface_create_for_texture:
 * @content: type of content in the surface
 * @tex: name of texture to use for storage of surface pixels
 * @width: width of the surface, in pixels
 * @height: height of the surface, in pixels
 *
 * Creates a GL surface for the specified texture with the specified
 * content and dimensions.  The texture must be kept around until the
 * #cairo_surface_t is destroyed or cairo_surface_finish() is called
 * on the surface.  The initial contents of @tex will be used as the
 * initial image contents; you must explicitly clear the buffer,
 * using, for example, cairo_rectangle() and cairo_fill() if you want
 * it cleared.  The format of @tex should be compatible with @content,
 * in the sense that it must have the color components required by
 * @content.
 *
 * Return value: a pointer to the newly created surface. The caller
 * owns the surface and should call cairo_surface_destroy() when done
 * with it.
 *
 * This function always returns a valid pointer, but it will return a
 * pointer to a "nil" surface if an error such as out of memory
 * occurs. You can use cairo_surface_status() to check for this.
 *
 * Since: TBD
 **/
cairo_surface_t *
cairo_gl_surface_create_for_texture (cairo_device_t	*abstract_device,
				     cairo_content_t	 content,
				     unsigned int	 tex,
				     int		 width,
				     int		 height)
{
    cairo_gl_context_t *ctx;
    cairo_gl_surface_t *surface;
    cairo_status_t status;

    if (! CAIRO_CONTENT_VALID (content))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_CONTENT));

    if (abstract_device == NULL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NULL_POINTER));

    if (abstract_device->status)
	return _cairo_surface_create_in_error (abstract_device->status);

    if (abstract_device->backend->type != CAIRO_DEVICE_TYPE_GL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_DEVICE_TYPE_MISMATCH));

    status = _cairo_gl_context_acquire (abstract_device, &ctx);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);

    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_scratch_for_texture (ctx, content,
						      tex, width, height, TRUE);
    status = _cairo_gl_context_release (ctx, status);

    return &surface->base;
}
slim_hidden_def (cairo_gl_surface_create_for_texture);


void
cairo_gl_surface_set_size (cairo_surface_t *abstract_surface,
			   int              width,
			   int              height)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if (unlikely (abstract_surface->status))
	return;
    if (unlikely (abstract_surface->finished)) {
	_cairo_surface_set_error (abstract_surface,
				  _cairo_error (CAIRO_STATUS_SURFACE_FINISHED));
        return;
    }

    if (! _cairo_surface_is_gl (abstract_surface) ||
        _cairo_gl_surface_is_texture (surface)) {
	_cairo_surface_set_error (abstract_surface,
				  _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH));
	return;
    }

    if (surface->width != width || surface->height != height) {
	surface->size_changed = TRUE;
	surface->width = width;
	surface->height = height;
    }
}

int
cairo_gl_surface_get_width (cairo_surface_t *abstract_surface)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if (! _cairo_surface_is_gl (abstract_surface))
	return 0;

    return surface->width;
}

int
cairo_gl_surface_get_height (cairo_surface_t *abstract_surface)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if (! _cairo_surface_is_gl (abstract_surface))
	return 0;

    return surface->height;
}

void
cairo_gl_surface_swapbuffers (cairo_surface_t *abstract_surface)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if (unlikely (abstract_surface->status))
	return;
    if (unlikely (abstract_surface->finished)) {
	_cairo_surface_set_error (abstract_surface,
				  _cairo_error (CAIRO_STATUS_SURFACE_FINISHED));
        return;
    }

    if (! _cairo_surface_is_gl (abstract_surface)) {
	_cairo_surface_set_error (abstract_surface,
				  CAIRO_STATUS_SURFACE_TYPE_MISMATCH);
	return;
    }

    if (! _cairo_gl_surface_is_texture (surface)) {
	cairo_gl_context_t *ctx;
        cairo_status_t status;

        status = _cairo_gl_context_acquire (surface->base.device, &ctx);
        if (unlikely (status))
            return;

	/* And in any case we should flush any pending operations. */
	_cairo_gl_composite_flush (ctx);

	/* For swapping on EGL, at least, we need a valid context/target. */
	_cairo_gl_context_set_destination (ctx, surface, FALSE);

	ctx->swap_buffers (ctx, surface);

	/* according to khronos specs on egl 1.4, stencil buffer is
	 * not preserved after eglSwapBuffers */
	if (surface->clip_on_stencil_buffer) {
	    _cairo_clip_destroy (surface->clip_on_stencil_buffer);
	    surface->clip_on_stencil_buffer = NULL;
	}

        status = _cairo_gl_context_release (ctx, status);
        if (status)
            status = _cairo_surface_set_error (abstract_surface, status);
    }
}

static cairo_bool_t
_cairo_gl_surface_size_valid (cairo_gl_surface_t *surface,
			      int width, int height)
{
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)surface->base.device;
    return width > 0 && height > 0 &&
	width <= ctx->max_framebuffer_size &&
	height <= ctx->max_framebuffer_size;
}

static cairo_surface_t *
_cairo_gl_surface_create_similar (void		 *abstract_surface,
				  cairo_content_t  content,
				  int		  width,
				  int		  height)
{
    cairo_surface_t *surface = abstract_surface;
    cairo_gl_context_t *ctx;
    cairo_status_t status;

    if (! _cairo_gl_surface_size_valid (abstract_surface, width, height))
        return _cairo_image_surface_create_with_content (content, width, height);

    status = _cairo_gl_context_acquire (surface->device, &ctx);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);

    surface = _cairo_gl_surface_create_and_clear_scratch (ctx, content, width, height);

    status = _cairo_gl_context_release (ctx, status);
    if (unlikely (status)) {
        cairo_surface_destroy (surface);
        return _cairo_surface_create_in_error (status);
    }

    return surface;
}

static cairo_int_status_t
_cairo_gl_surface_fill_alpha_channel (cairo_gl_surface_t *dst,
				      cairo_gl_context_t *ctx,
				      int x, int y,
				      int width, int height)
{
    cairo_gl_composite_t setup;
    cairo_status_t status;

    _cairo_gl_composite_flush (ctx);
    ctx->dispatch.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);

    status = _cairo_gl_composite_init (&setup, CAIRO_OPERATOR_SOURCE,
				       dst, FALSE);
    if (unlikely (status))
        goto CLEANUP;

    _cairo_gl_composite_set_solid_source (&setup, CAIRO_COLOR_BLACK);

    status = _cairo_gl_composite_begin (&setup, &ctx);
    if (unlikely (status))
        goto CLEANUP;

    _cairo_gl_context_emit_rect (ctx, x, y, x + width, y + height);

    status = _cairo_gl_context_release (ctx, status);

  CLEANUP:
    _cairo_gl_composite_fini (&setup);

    _cairo_gl_composite_flush (ctx);
    ctx->dispatch.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    return status;
}

cairo_status_t
_cairo_gl_surface_draw_image (cairo_gl_surface_t *dst,
			      cairo_image_surface_t *src,
			      int src_x, int src_y,
			      int width, int height,
			      int dst_x, int dst_y,
			      cairo_bool_t force_flush)
{
    GLenum internal_format, format, type;
    cairo_bool_t has_alpha, needs_swap;
    cairo_image_surface_t *clone = NULL;
    cairo_gl_context_t *ctx;
    int cpp;
    cairo_image_surface_t *rgba_clone = NULL;
    cairo_int_status_t status = CAIRO_INT_STATUS_SUCCESS;

    status = _cairo_gl_context_acquire (dst->base.device, &ctx);
    if (unlikely (status))
	return status;

    if (_cairo_gl_get_flavor (&ctx->dispatch) == CAIRO_GL_FLAVOR_ES2 ||
	_cairo_gl_get_flavor (&ctx->dispatch) == CAIRO_GL_FLAVOR_ES3) {
	pixman_format_code_t pixman_format;
	cairo_surface_pattern_t pattern;
	cairo_bool_t require_conversion = FALSE;
	pixman_format = _cairo_is_little_endian () ? PIXMAN_a8b8g8r8 : PIXMAN_r8g8b8a8;

	if (src->base.content != CAIRO_CONTENT_ALPHA) {
	    if (src->pixman_format != pixman_format)
		require_conversion = TRUE;
	}
	else if (dst->base.content != CAIRO_CONTENT_ALPHA)
	    require_conversion = TRUE;
	else {
	    if (src->pixman_format == PIXMAN_a1) {
		pixman_format = PIXMAN_a8;
		require_conversion = TRUE;
	    }
	}

	if (require_conversion) {
	    rgba_clone = (cairo_image_surface_t *)
		_cairo_image_surface_create_with_pixman_format (NULL,
								pixman_format,
								src->width,
								src->height,
								0);
	    if (unlikely (rgba_clone->base.status))
		goto FAIL;

	    _cairo_pattern_init_for_surface (&pattern, &src->base);
	    status = _cairo_surface_paint (&rgba_clone->base,
					   CAIRO_OPERATOR_SOURCE,
					   &pattern.base, NULL);
	    _cairo_pattern_fini (&pattern.base);
	    if (unlikely (status))
		goto FAIL;

	    src = rgba_clone;
	}
    }

    if (! _cairo_gl_get_image_format_and_type (ctx->gl_flavor,
					       src->pixman_format,
					       &internal_format,
					       &format,
					       &type,
					       &has_alpha,
					       &needs_swap))
    {
	cairo_bool_t is_supported;

	clone = _cairo_image_surface_coerce (src);
	if (unlikely (status = clone->base.status))
	    goto FAIL;

	is_supported =
	    _cairo_gl_get_image_format_and_type (ctx->gl_flavor,
						 clone->pixman_format,
		                                 &internal_format,
						 &format,
						 &type,
						 &has_alpha,
						 &needs_swap);
	assert (is_supported);
	assert (!needs_swap);
	src = clone;
    }

    cpp = PIXMAN_FORMAT_BPP (src->pixman_format) / 8;

    if (force_flush) {
	status = _cairo_gl_surface_flush (&dst->base, 0);
	if (unlikely (status))
	    goto FAIL;
    }

    if (_cairo_gl_surface_is_texture (dst)) {
	void *data_start = src->data + src_y * src->stride + src_x * cpp;
	void *data_start_gles2 = NULL;

	/*
	 * Due to GL_UNPACK_ROW_LENGTH missing in GLES2 we have to extract the
	 * image data ourselves in some cases. In particular, we must extract
	 * the pixels if:
	 * a. we don't want full-length lines or
	 * b. the row stride cannot be handled by GL itself using a 4 byte
	 *     alignment constraint
	 */
	if (src->stride < 0 ||
	    (ctx->gl_flavor == CAIRO_GL_FLAVOR_ES2 &&
	     (src->width * cpp < src->stride - 3 ||
	      width != src->width)))
	{
	    ctx->dispatch.PixelStorei (GL_UNPACK_ALIGNMENT, 1);
	    status = _cairo_gl_surface_extract_image_data (src, src_x, src_y,
							   width, height,
							   &data_start_gles2);
	    if (unlikely (status))
		goto FAIL;

	    data_start = data_start_gles2;
	}
	else
	{
	    ctx->dispatch.PixelStorei (GL_UNPACK_ALIGNMENT, 4);
	    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP ||
		ctx->gl_flavor == CAIRO_GL_FLAVOR_ES3)
		ctx->dispatch.PixelStorei (GL_UNPACK_ROW_LENGTH, src->stride / cpp);
	}

	/* we must resolve the renderbuffer to texture before we
	   upload image */
	status = _cairo_gl_surface_resolve_multisampling (dst);
    if (unlikely (status)) {
        free (data_start_gles2);
        goto FAIL;
    }

        _cairo_gl_context_activate (ctx, CAIRO_GL_TEX_TEMP);
	ctx->dispatch.BindTexture (ctx->tex_target, dst->tex);
	ctx->dispatch.TexParameteri (ctx->tex_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	ctx->dispatch.TexParameteri (ctx->tex_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	ctx->dispatch.TexSubImage2D (ctx->tex_target, 0,
			 dst_x, dst_y, width, height,
			 format, type, data_start);

	free (data_start_gles2);

	/* If we just treated some rgb-only data as rgba, then we have to
	 * go back and fix up the alpha channel where we filled in this
	 * texture data.
	 */
	if (!has_alpha) {
	    _cairo_gl_surface_fill_alpha_channel (dst, ctx,
						  dst_x, dst_y,
						  width, height);

	}
	dst->content_synced = FALSE;
    } else {
        cairo_surface_t *tmp;

        tmp = _cairo_gl_surface_create_scratch (ctx,
                                                dst->base.content,
                                                width, height);
        if (unlikely (tmp->status))
            goto FAIL;

        status = _cairo_gl_surface_draw_image ((cairo_gl_surface_t *) tmp,
                                               src,
                                               src_x, src_y,
                                               width, height,
                                               0, 0, force_flush);
        if (status == CAIRO_INT_STATUS_SUCCESS) {
            cairo_surface_pattern_t tmp_pattern;
	    cairo_rectangle_int_t r;
	    cairo_clip_t *clip;

            _cairo_pattern_init_for_surface (&tmp_pattern, tmp);
	    cairo_matrix_init_translate (&tmp_pattern.base.matrix,
					 -dst_x, -dst_y);
	    tmp_pattern.base.filter = CAIRO_FILTER_NEAREST;
	    tmp_pattern.base.extend = CAIRO_EXTEND_NONE;

	    r.x = dst_x;
	    r.y = dst_y;
	    r.width = width;
	    r.height = height;
	    clip = _cairo_clip_intersect_rectangle (NULL, &r);
	    status = _cairo_surface_paint (&dst->base,
					   CAIRO_OPERATOR_SOURCE,
					   &tmp_pattern.base,
					   clip);
	    _cairo_clip_destroy (clip);
            _cairo_pattern_fini (&tmp_pattern.base);
        }

        cairo_surface_destroy (tmp);
    }

FAIL:
    status = _cairo_gl_context_release (ctx, status);

    if (clone)
        cairo_surface_destroy (&clone->base);

    if (rgba_clone)
	cairo_surface_destroy (&rgba_clone->base);

    if (status == CAIRO_INT_STATUS_SUCCESS) {
	dst->content_changed = TRUE;
	dst->content_synced = FALSE;
    }

    return status;
}

static int _cairo_gl_surface_flavor (cairo_gl_surface_t *surface)
{
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)surface->base.device;
    return ctx->gl_flavor;
}

static cairo_status_t
_cairo_gl_surface_finish (void *abstract_surface)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_status_t status;
    cairo_gl_context_t *ctx;

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
        return status;

    if ((ctx->operands[CAIRO_GL_TEX_SOURCE].type == CAIRO_GL_OPERAND_TEXTURE ||
	 ctx->operands[CAIRO_GL_TEX_SOURCE].type == CAIRO_GL_OPERAND_GAUSSIAN) &&
        ctx->operands[CAIRO_GL_TEX_SOURCE].texture.surface == surface)
        _cairo_gl_context_destroy_operand (ctx, CAIRO_GL_TEX_SOURCE);
    if ((ctx->operands[CAIRO_GL_TEX_MASK].type == CAIRO_GL_OPERAND_TEXTURE ||
	 ctx->operands[CAIRO_GL_TEX_MASK].type == CAIRO_GL_OPERAND_GAUSSIAN) &&
        ctx->operands[CAIRO_GL_TEX_MASK].texture.surface == surface)
        _cairo_gl_context_destroy_operand (ctx, CAIRO_GL_TEX_MASK);
    if (ctx->current_target == surface)
	ctx->current_target = NULL;

    if (surface->fb)
        ctx->dispatch.DeleteFramebuffers (1, &surface->fb);
    if (surface->depth_stencil)
        ctx->dispatch.DeleteRenderbuffers (1, &surface->depth_stencil);
    if (surface->owns_tex)
	ctx->dispatch.DeleteTextures (1, &surface->tex);

    if (surface->msaa_depth_stencil)
	ctx->dispatch.DeleteRenderbuffers (1, &surface->msaa_depth_stencil);
    if (surface->msaa_fb)
	ctx->dispatch.DeleteFramebuffers (1, &surface->msaa_fb);
    if (surface->msaa_rb)
	ctx->dispatch.DeleteRenderbuffers (1, &surface->msaa_rb);

    if (surface->image_node) {
        surface->image_node->node.pinned = FALSE;
	_cairo_rtree_node_remove (&ctx->image_cache->rtree,
				  &surface->image_node->node);
    }

    if (surface->clip_on_stencil_buffer)
        _cairo_clip_destroy (surface->clip_on_stencil_buffer);

    return _cairo_gl_context_release (ctx, status);
}

static cairo_image_surface_t *
_cairo_gl_surface_map_to_image (void      *abstract_surface,
				const cairo_rectangle_int_t   *extents)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_image_surface_t *image;
    cairo_gl_context_t *ctx;
    GLenum format, type;
    pixman_format_code_t pixman_format;
    unsigned int cpp;
    cairo_bool_t flipped, mesa_invert;
    cairo_status_t status;
    int y;

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status)) {
	return _cairo_image_surface_create_in_error (status);
    }

    /* Want to use a switch statement here but the compiler gets whiny. */
    if (surface->base.content == CAIRO_CONTENT_COLOR_ALPHA) {
	format = GL_BGRA;
	pixman_format = PIXMAN_a8r8g8b8;
	type = GL_UNSIGNED_INT_8_8_8_8_REV;
	cpp = 4;
    } else if (surface->base.content == CAIRO_CONTENT_COLOR) {
	format = GL_BGRA;
	pixman_format = PIXMAN_x8r8g8b8;
	type = GL_UNSIGNED_INT_8_8_8_8_REV;
	cpp = 4;
    } else if (surface->base.content == CAIRO_CONTENT_ALPHA) {
	format = GL_ALPHA;
	pixman_format = PIXMAN_a8;
	type = GL_UNSIGNED_BYTE;
	cpp = 1;
    } else {
	ASSERT_NOT_REACHED;
	return NULL;
    }

    if (_cairo_gl_surface_flavor (surface) == CAIRO_GL_FLAVOR_ES2 ||
	_cairo_gl_surface_flavor (surface) == CAIRO_GL_FLAVOR_ES3) {
	/* If only RGBA is supported, we must download data in a compatible
	 * format. This means that pixman will convert the data on the CPU when
	 * interacting with other image surfaces. For ALPHA, GLES2 does not
	 * support GL_PACK_ROW_LENGTH anyway, and this makes sure that the
	 * pixman image that is created has row_stride = row_width * bpp. */
	if (surface->base.content == CAIRO_CONTENT_ALPHA || !ctx->can_read_bgra) {
	    cairo_bool_t little_endian = _cairo_is_little_endian ();
	    format = GL_RGBA;

	    if (surface->base.content == CAIRO_CONTENT_COLOR) {
		pixman_format = little_endian ?
		    PIXMAN_x8b8g8r8 : PIXMAN_r8g8b8x8;
	    } else {
		pixman_format = little_endian ?
		    PIXMAN_a8b8g8r8 : PIXMAN_r8g8b8a8;
	    }
	}

	/* GLES2 only supports GL_UNSIGNED_BYTE. */
	type = GL_UNSIGNED_BYTE;
	cpp = 4;
    }

    image = (cairo_image_surface_t*)
	_cairo_image_surface_create_with_pixman_format (NULL,
							pixman_format,
							extents->width,
							extents->height,
							-1);
    if (unlikely (image->base.status)) {
	status = _cairo_gl_context_release (ctx, status);
	return image;
    }

    cairo_surface_set_device_offset (&image->base, -extents->x, -extents->y);

    /* If the original surface has not been modified or
     * is clear, we can avoid downloading data. */
    if (surface->base.is_clear || surface->base.serial == 0) {
	status = _cairo_gl_context_release (ctx, status);
	return image;
    }

    /* This is inefficient, as we'd rather just read the thing without making
     * it the destination.  But then, this is the fallback path, so let's not
     * fall back instead.
     */
    _cairo_gl_composite_flush (ctx);

    _cairo_gl_context_set_destination (ctx, surface, FALSE);

    flipped = ! _cairo_gl_surface_is_texture (surface);
    mesa_invert = flipped && ctx->has_mesa_pack_invert;

    ctx->dispatch.PixelStorei (GL_PACK_ALIGNMENT, 4);
    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP ||
	ctx->gl_flavor == CAIRO_GL_FLAVOR_ES3)
	ctx->dispatch.PixelStorei (GL_PACK_ROW_LENGTH, image->stride / cpp);
    if (mesa_invert)
	ctx->dispatch.PixelStorei (GL_PACK_INVERT_MESA, 1);

    y = extents->y;
    if (flipped)
	y = surface->height - extents->y - extents->height;

    ctx->dispatch.ReadPixels (extents->x, y,
		  extents->width, extents->height,
		  format, type, image->data);
    if (mesa_invert)
	ctx->dispatch.PixelStorei (GL_PACK_INVERT_MESA, 0);

    status = _cairo_gl_context_release (ctx, status);
    if (unlikely (status)) {
	cairo_surface_destroy (&image->base);
	return _cairo_image_surface_create_in_error (status);
    }

    /* We must invert the image manualy if we lack GL_MESA_pack_invert */
    if (flipped && ! mesa_invert) {
	uint8_t stack[1024], *row = stack;
	uint8_t *top = image->data;
	uint8_t *bot = image->data + (image->height-1)*image->stride;

	if (image->stride > (int)sizeof(stack)) {
	    row = malloc (image->stride);
	    if (unlikely (row == NULL)) {
		cairo_surface_destroy (&image->base);
		return _cairo_image_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));
	    }
	}

	while (top < bot) {
	    memcpy (row, top, image->stride);
	    memcpy (top, bot, image->stride);
	    memcpy (bot, row, image->stride);
	    top += image->stride;
	    bot -= image->stride;
	}

	if (row != stack)
	    free(row);
    }

    image->base.is_clear = FALSE;

    return image;
}

static cairo_surface_t *
_cairo_gl_surface_source (void		       *abstract_surface,
			  cairo_rectangle_int_t *extents)
{
    cairo_gl_surface_t *surface = abstract_surface;

    if (extents) {
	extents->x = extents->y = 0;
	extents->width  = surface->width;
	extents->height = surface->height;
    }

    return &surface->base;
}

static cairo_status_t
_cairo_gl_surface_acquire_source_image (void		       *abstract_surface,
					cairo_image_surface_t **image_out,
					void		      **image_extra)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_rectangle_int_t extents;

    *image_extra = NULL;

    extents.x = extents.y = 0;
    extents.width = surface->width;
    extents.height = surface->height;

    *image_out = (cairo_image_surface_t *)
	_cairo_gl_surface_map_to_image (surface, &extents);
    return (*image_out)->base.status;
}

static void
_cairo_gl_surface_release_source_image (void		      *abstract_surface,
					cairo_image_surface_t *image,
					void		      *image_extra)
{
    cairo_surface_destroy (&image->base);
}

static cairo_int_status_t
_cairo_gl_surface_unmap_image (void		      *abstract_surface,
			       cairo_image_surface_t *image)
{
    cairo_int_status_t status;

    status = _cairo_gl_surface_draw_image (abstract_surface, image,
					   0, 0,
					   image->width, image->height,
					   image->base.device_transform_inverse.x0,
					   image->base.device_transform_inverse.y0,
					   TRUE);

    cairo_surface_finish (&image->base);
    cairo_surface_destroy (&image->base);

    return status;
}

static cairo_bool_t
_cairo_gl_surface_get_extents (void		     *abstract_surface,
			       cairo_rectangle_int_t *rectangle)
{
    cairo_gl_surface_t *surface = abstract_surface;

    rectangle->x = 0;
    rectangle->y = 0;
    rectangle->width  = surface->width;
    rectangle->height = surface->height;

    return TRUE;
}

static cairo_status_t
_cairo_gl_surface_flush (void *abstract_surface, unsigned flags)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_status_t status;
    cairo_gl_context_t *ctx;

    if (flags)
	return CAIRO_STATUS_SUCCESS;

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
        return status;

    if (((ctx->operands[CAIRO_GL_TEX_SOURCE].type == CAIRO_GL_OPERAND_TEXTURE ||
	  ctx->operands[CAIRO_GL_TEX_SOURCE].type == CAIRO_GL_OPERAND_GAUSSIAN) &&
         ctx->operands[CAIRO_GL_TEX_SOURCE].texture.surface == surface) ||
        ((ctx->operands[CAIRO_GL_TEX_MASK].type == CAIRO_GL_OPERAND_TEXTURE ||
	  ctx->operands[CAIRO_GL_TEX_MASK].type == CAIRO_GL_OPERAND_GAUSSIAN) &&
         ctx->operands[CAIRO_GL_TEX_MASK].texture.surface == surface) ||
        (ctx->current_target == surface))
      _cairo_gl_composite_flush (ctx);

    status = _cairo_gl_surface_resolve_multisampling (surface);

    if (ctx->msaa_type != CAIRO_GL_NONE_MULTISAMPLE_TO_TEXTURE)
        ctx->dispatch.Flush ();

    return _cairo_gl_context_release (ctx, status);
}

cairo_int_status_t
_cairo_gl_surface_resolve_multisampling (cairo_gl_surface_t *surface)
{
    cairo_gl_context_t *ctx;
    cairo_int_status_t status;

    if (surface->base.device == NULL)
	return CAIRO_INT_STATUS_SUCCESS;

    if (! surface->content_cleared) {
	/* GLES surfaces do not need explicit resolution. */
	if (! surface->msaa_active)
	    return CAIRO_INT_STATUS_SUCCESS;
	else if (((cairo_gl_context_t *) surface->base.device)->gl_flavor == CAIRO_GL_FLAVOR_ES2 &&
		 !((cairo_gl_context_t *) surface->base.device)->has_angle_multisampling)
	    return CAIRO_INT_STATUS_SUCCESS;
	else if (! _cairo_gl_surface_is_texture (surface))
	    return CAIRO_INT_STATUS_SUCCESS;
    }

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
	return status;

    _cairo_gl_context_set_destination (ctx, surface, FALSE);

    status = _cairo_gl_context_release (ctx, status);

    surface->content_cleared = FALSE;

    return status;
}

static const cairo_compositor_t *
get_compositor (cairo_gl_surface_t *surface)
{
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)surface->base.device;
    return ctx->compositor;
}

static cairo_int_status_t
_cairo_gl_surface_paint (void			*surface,
			 cairo_operator_t	 op,
			 const cairo_pattern_t	*source,
			 const cairo_clip_t	*clip)
{
    cairo_int_status_t status;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *)surface;
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)dst->base.device;

    status = cairo_device_acquire (dst->base.device);
    if (unlikely (status))
	return status;

    status = _cairo_surface_shadow_paint (surface, op, source, clip,
					  &source->shadow);
    ctx->source_scratch_in_use = FALSE;
    if (unlikely (status)) {
	cairo_device_release (dst->base.device);
	return status;
    }

    if (source->shadow.draw_shadow_only) {
	if (status == CAIRO_INT_STATUS_SUCCESS) {
	    dst->content_changed = TRUE;
	    dst->content_synced = FALSE;
	}

	ctx->source_scratch_in_use = FALSE;
	cairo_device_release (dst->base.device);
	return status;
    }

#if 0 // Currently glClear does not get flushed to GPU so do not use this fast path for the time being
    /* simplify the common case of clearing the surface */
    if (clip == NULL) {
        if (op == CAIRO_OPERATOR_CLEAR) {
            status = _cairo_gl_surface_clear (surface, CAIRO_COLOR_TRANSPARENT);
	    cairo_device_release (dst->base.device);
	    return status;
	}
	else if (source->type == CAIRO_PATTERN_TYPE_SOLID &&
                (op == CAIRO_OPERATOR_SOURCE ||
                 (op == CAIRO_OPERATOR_OVER && _cairo_pattern_is_opaque_solid (source)))) {
            status = _cairo_gl_surface_clear (surface,
                                            &((cairo_solid_pattern_t *) source)->color);
	    cairo_device_release (dst->base.device);
	    return status;
        }
    }
#endif

    status = _cairo_compositor_paint (get_compositor (surface), surface,
				      op, source, clip);
    if (status == CAIRO_INT_STATUS_SUCCESS) {
	dst->content_changed = TRUE;
	dst->content_synced = FALSE;
    }

    ctx->source_scratch_in_use = FALSE;
    cairo_device_release (dst->base.device);
    return status;
}

static cairo_int_status_t
_cairo_gl_surface_mask (void			 *surface,
			cairo_operator_t	  op,
			const cairo_pattern_t	*source,
			const cairo_pattern_t	*mask,
			const cairo_clip_t	*clip)
{
    cairo_int_status_t status;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *) surface;
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)dst->base.device;

    status = cairo_device_acquire (dst->base.device);
    if (unlikely (status))
	return status;

    status = _cairo_surface_shadow_mask (surface, op, source, mask, clip,
					  &source->shadow);
    ctx->source_scratch_in_use = FALSE;
    if (unlikely (status)) {
	cairo_device_release (dst->base.device);
	return status;
    }

    if (source->shadow.draw_shadow_only) {
	if (status == CAIRO_INT_STATUS_SUCCESS) {
	    dst->content_changed = TRUE;
	    dst->content_synced = FALSE;
	}

	ctx->source_scratch_in_use = FALSE;
	cairo_device_release (dst->base.device);
	return status;
    }

    status = _cairo_compositor_mask (get_compositor (surface), surface,
				     op, source, mask, clip);
    if (status == CAIRO_INT_STATUS_SUCCESS) {
	dst->content_changed = TRUE;
	dst->content_synced = FALSE;

	ctx->source_scratch_in_use = FALSE;
	cairo_device_release (dst->base.device);
	return status;
    }

    status = _cairo_compositor_mask (get_compositor (surface), surface,
				     op, source, mask, clip);
    if (status == CAIRO_INT_STATUS_SUCCESS) {
	dst->content_changed = TRUE;
	dst->content_synced = FALSE;
    }

    ctx->source_scratch_in_use = FALSE;
    cairo_device_release (dst->base.device);
    return status;
}

static cairo_int_status_t
_cairo_gl_surface_stroke (void			        *surface,
                          cairo_operator_t		 op,
                          const cairo_pattern_t	        *source,
                          const cairo_path_fixed_t	*path,
                          const cairo_stroke_style_t	*style,
                          const cairo_matrix_t	        *ctm,
                          const cairo_matrix_t	        *ctm_inverse,
                          double			 tolerance,
                          cairo_antialias_t		 antialias,
                          const cairo_clip_t		*clip)
{
    cairo_int_status_t status;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *)surface;
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)dst->base.device;
    cairo_shadow_type_t shadow_type = source->shadow.type;

    status = cairo_device_acquire (dst->base.device);
    if (unlikely (status))
	return status;

    if (shadow_type != CAIRO_SHADOW_INSET)
	status = _cairo_surface_shadow_stroke (surface, op, source, path,
					       style, ctm, ctm_inverse,
					       tolerance, antialias,
					       clip, &source->shadow);

    ctx->source_scratch_in_use = FALSE;
    if (unlikely (status)) {
 	cairo_device_release (dst->base.device);
	return status;
    }

    dst->content_changed = TRUE;
    dst->content_synced = FALSE;

    if (shadow_type == CAIRO_SHADOW_DROP &&
	source->shadow.draw_shadow_only) {
	ctx->source_scratch_in_use = FALSE;
	cairo_device_release (dst->base.device);
	return status;
    }

    ctx->source_scratch_in_use = FALSE;

    if (! source->shadow.draw_shadow_only)
	status = _cairo_compositor_stroke (get_compositor (surface), surface,
				           op, source, path, style,
				           ctm, ctm_inverse, tolerance,
				           antialias, clip);
    if (unlikely (status)) {
 	ctx->source_scratch_in_use = FALSE;
  	cairo_device_release (dst->base.device);
	return status;
    }

    ctx->source_scratch_in_use = FALSE;

    if (shadow_type == CAIRO_SHADOW_INSET)
	status = _cairo_surface_shadow_stroke (surface, op, source, path,
					       style, ctm, ctm_inverse,
					       tolerance, antialias,
					       clip, &source->shadow);

    ctx->source_scratch_in_use = FALSE;
    cairo_device_release (dst->base.device);
    return status;
}

static cairo_int_status_t
_cairo_gl_surface_fill (void			*surface,
                        cairo_operator_t	 op,
                        const cairo_pattern_t	*source,
                        const cairo_path_fixed_t*path,
                        cairo_fill_rule_t	 fill_rule,
                        double			 tolerance,
                        cairo_antialias_t	 antialias,
                        const cairo_clip_t	*clip)
{
    cairo_status_t status;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *)surface;
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)dst->base.device;
    cairo_shadow_type_t shadow_type = source->shadow.type;

    status = cairo_device_acquire (dst->base.device);
    if (unlikely (status))
	return status;

    if (shadow_type != CAIRO_SHADOW_INSET)
	status = _cairo_surface_shadow_fill (surface, op, source, path,
					     fill_rule, tolerance, antialias,
					     clip, &source->shadow);

    ctx->source_scratch_in_use = FALSE;
    if (unlikely (status)) {
 	cairo_device_release (dst->base.device);
	return status;
    }

    dst->content_changed = TRUE;
    dst->content_synced = FALSE;

    if (shadow_type == CAIRO_SHADOW_DROP &&
	source->shadow.draw_shadow_only) {
	ctx->source_scratch_in_use = FALSE;
	cairo_device_release (dst->base.device);
	return status;
    }

    ctx->source_scratch_in_use = FALSE;

    if (! source->shadow.draw_shadow_only) {
	if (! source->shadow.path_is_fill_with_spread ||
	    source->shadow.type != CAIRO_SHADOW_INSET)
	    status = _cairo_compositor_fill (get_compositor (surface),
					     surface,
					     op, source, path,
					     fill_rule, tolerance,
					     antialias,
					     clip);
	else
	    status = _cairo_compositor_paint (get_compositor (surface),
					      surface, op, source,
					      clip);
    }

    if (unlikely (status)) {
 	ctx->source_scratch_in_use = FALSE;
  	cairo_device_release (dst->base.device);
	return status;
    }

    ctx->source_scratch_in_use = FALSE;

    if (shadow_type == CAIRO_SHADOW_INSET)
	status = _cairo_surface_shadow_fill (surface, op, source, path,
					     fill_rule, tolerance, antialias,
					     clip, &source->shadow);

    ctx->source_scratch_in_use = FALSE;
    cairo_device_release (dst->base.device);

    return status;
}

static cairo_int_status_t
_cairo_gl_surface_glyphs (void			*surface,
			  cairo_operator_t	 op,
			  const cairo_pattern_t	*source,
			  cairo_glyph_t		*glyphs,
			  int			 num_glyphs,
			  cairo_scaled_font_t	*font,
			  const cairo_clip_t	*clip)
{
    cairo_int_status_t status;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *)surface;
    cairo_gl_context_t *ctx = (cairo_gl_context_t *)dst->base.device;
    cairo_shadow_type_t shadow_type = source->shadow.type;

    status = cairo_device_acquire (dst->base.device);
    if (unlikely (status))
	return status;

    if (shadow_type != CAIRO_SHADOW_INSET)
	status = _cairo_surface_shadow_glyphs (surface, op, source,
					       font,
					       glyphs, num_glyphs,
					       clip, &source->shadow);

    ctx->source_scratch_in_use = FALSE;
    if (unlikely (status)) {
 	cairo_device_release (dst->base.device);
	return status;
    }

    dst->content_changed = TRUE;
    dst->content_synced = FALSE;

    if (shadow_type == CAIRO_SHADOW_DROP &&
	source->shadow.draw_shadow_only) {
	ctx->source_scratch_in_use = FALSE;
	cairo_device_release (dst->base.device);
	return status;
    }

    ctx->source_scratch_in_use = FALSE;

    if (! source->shadow.draw_shadow_only)
	status = _cairo_compositor_glyphs (get_compositor (surface), surface,
				           op, source, glyphs, num_glyphs,
				           font, clip);

    if (unlikely (status)) {
 	ctx->source_scratch_in_use = FALSE;
  	cairo_device_release (dst->base.device);
	return status;
    }

    ctx->source_scratch_in_use = FALSE;

    if (shadow_type == CAIRO_SHADOW_INSET)
	status = _cairo_surface_shadow_glyphs (surface, op, source,
					       font,
					       glyphs, num_glyphs,
					       clip, &source->shadow);

    ctx->source_scratch_in_use = FALSE;
    cairo_device_release (dst->base.device);
    return status;
}

static const cairo_surface_backend_t _cairo_gl_surface_backend = {
    CAIRO_SURFACE_TYPE_GL,
    _cairo_gl_surface_finish,
    _cairo_default_context_create,

    _cairo_gl_surface_create_similar,
    NULL, /* similar image */
    _cairo_gl_surface_map_to_image,
    _cairo_gl_surface_unmap_image,

    _cairo_gl_surface_source,
    _cairo_gl_surface_acquire_source_image,
    _cairo_gl_surface_release_source_image,
    NULL, /* snapshot */

    NULL, /* copy_page */
    NULL, /* show_page */

    _cairo_gl_surface_get_extents,
    _cairo_image_surface_get_font_options,

    _cairo_gl_surface_flush,
    NULL, /* mark_dirty_rectangle */

    _cairo_gl_surface_paint,
    _cairo_gl_surface_mask,
    _cairo_gl_surface_stroke,
    _cairo_gl_surface_fill,
    NULL, /* fill/stroke */
    _cairo_gl_surface_glyphs,
    NULL, /* has_text_glyphs */
    NULL, /* show_text_glyphs */
    NULL, /* get_supported_mime_types */
    _cairo_gl_surface_shadow_surface,
    _cairo_gl_surface_glyph_shadow_surface,
    _cairo_gl_surface_shadow_mask_surface,
    _cairo_gl_surface_glyph_shadow_mask_surface,
};

cairo_status_t
cairo_gl_surface_set_binding_texture (cairo_surface_t *abstract_surface,
			       unsigned int    texture)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if ((cairo_surface_get_type (&surface->base) != CAIRO_SURFACE_TYPE_GL) ||
	surface->tex)
	return CAIRO_STATUS_SURFACE_TYPE_MISMATCH;

    surface->bounded_tex = texture;
    surface->operand.texture.tex = texture;

    return CAIRO_STATUS_SUCCESS;
}
