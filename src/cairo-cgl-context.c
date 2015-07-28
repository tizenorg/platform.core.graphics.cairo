/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Eric Anholt
 * Copyright © 2009 Chris Wilson
 * Copyright © 2005 Red Hat, Inc
 * Copyright © 2015 Samsung Research America, Inc. - Silicon Valley
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
 *	Carl Worth <cworth@cworth.org>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 *	Henry Songn <hsong@sisa.samsung.com>
 */

#include "cairoint.h"

#include "cairo-gl-private.h"

#include "cairo-error-private.h"

#include <dlfcn.h>

typedef struct _cairo_cgl_context {
    cairo_gl_context_t base;

    CGLContextObj context;
    CGLContextObj prev_context;
} cairo_cgl_context_t;

typedef struct _cairo_cgl_surface {
    cairo_gl_surface_t base;

    CGLContextObj context;
} cairo_cgl_surface_t;

static void *
_cgl_query_current_state (cairo_cgl_context_t * ctx)
{
    ctx->prev_context = CGLGetCurrentContext ();
}

static void
_cgl_acquire (void *abstract_ctx)
{
    cairo_cgl_context_t *ctx = abstract_ctx;
    _cgl_query_current_state (ctx);

    if (ctx->prev_context == ctx->context)
 	return;

    _cairo_gl_context_reset (&ctx->base);

    CGLLockContext (ctx->context);
    CGLSetCurrentContext (ctx->context);
}

static void
_cgl_make_current (void *abstract_ctx, cairo_gl_surface_t *abstract_surface)
{
    cairo_cgl_context_t *ctx = abstract_ctx;
    cairo_cgl_surface_t *surface = (cairo_cgl_surface_t *) abstract_surface;

    /* Set the window as the target of our context. */
    if (ctx->context != surface->context) {
	CGLLockContext (surface->context);
	CGLSetCurrentContext (surface->context);
    }
}

static void
_cgl_release (void *abstract_ctx)
{
    cairo_cgl_context_t *ctx = abstract_ctx;

    if (!ctx->base.thread_aware)
	return;

    CGLSetCurrentContext (NULL);
    CGLUnlockContext (ctx->context);
}

static void
_cgl_swap_buffers (void *abstract_ctx,
		   cairo_gl_surface_t *abstract_surface)
{
    cairo_cgl_context_t *ctx = abstract_ctx;
    cairo_cgl_surface_t *surface = (cairo_cgl_surface_t *)abstract_surface;

    /* is it possible to have different context? */
    if (ctx->context == surface->context)
	CGLFlushDrawable (surface->context);
}

static void
_cgl_destroy (void *abstract_ctx)
{
    cairo_cgl_context_t *ctx = abstract_ctx;

    CGLSetCurrentContext (NULL);
    CGLReleaseContext (ctx->context);
    CGLUnlockContext (ctx->context);
}

static cairo_gl_generic_func_t
_cairo_cgl_get_proc_address (void *data, const char *name)
{
    return dlsym (RTLD_DEFAULT, name);
}

cairo_device_t *
cairo_cgl_device_create (CGLContextObj context)
{
    cairo_cgl_context_t *ctx;
    cairo_status_t status;

    ctx = calloc (1, sizeof (cairo_cgl_context_t));
    if (unlikely (ctx == NULL))
	return _cairo_gl_context_create_in_error (CAIRO_STATUS_NO_MEMORY);

    ctx->context = CGLRetainContext (context);

    ctx->base.acquire = _cgl_acquire;
    ctx->base.release = _cgl_release;
    ctx->base.make_current = _cgl_make_current;
    ctx->base.swap_buffers = _cgl_swap_buffers;
    ctx->base.destroy = _cgl_destroy;

    _cgl_query_current_state (ctx);
    if (ctx->context != ctx->prev_context) {
	CGLLockContext (ctx->context);
	CGLSetCurrentContext (ctx->context);
    }

    status = _cairo_gl_dispatch_init (&ctx->base.dispatch,
				      (cairo_gl_get_proc_addr_func_t) _cairo_cgl_get_proc_address,
				      NULL);

    if (unlikely (status)) {
	ctx->base.release (ctx);
	CGLReleaseContext (ctx->context);
	free (ctx);
	return _cairo_gl_context_create_in_error (status);
    }

    status = _cairo_gl_context_init (&ctx->base);
    if (unlikely (status)) {
	ctx->base.release (ctx);
	CGLReleaseContext (ctx->context);
	free (ctx);
	return _cairo_gl_context_create_in_error (status);
    }

    ctx->base.release (ctx);

    return &ctx->base.base;
}

CGLContextObj
cairo_cgl_device_get_context (cairo_device_t *device)
{
    cairo_cgl_context_t *ctx;

    if (device->backend->type != CAIRO_DEVICE_TYPE_GL) {
	_cairo_error_throw (CAIRO_STATUS_DEVICE_TYPE_MISMATCH);
	return NULL;
    }

    ctx = (cairo_cgl_context_t *) device;

    return ctx->context;
}

cairo_surface_t *
cairo_gl_surface_create_for_cgl (cairo_device_t	*device,
				 int			 width,
				 int			 height)
{
    cairo_cgl_surface_t *surface;

    if (unlikely (device->status))
	return _cairo_surface_create_in_error (device->status);

    if (device->backend->type != CAIRO_DEVICE_TYPE_GL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH));

    if (width <= 0 || height <= 0)
        return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_SIZE));

    surface = calloc (1, sizeof (cairo_cgl_surface_t));
    if (unlikely (surface == NULL))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));

    _cairo_gl_surface_init (device, &surface->base,
			    CAIRO_CONTENT_COLOR_ALPHA, width, height);
    surface->context = ((cairo_cgl_context_t *) device)->context; 

    return &surface->base.base;
}
