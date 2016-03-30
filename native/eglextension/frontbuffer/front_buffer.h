/***************************************************************************
 * Ignoring the back buffer.
 ***************************************************************************/

#ifndef EGL_FRONTBUFFER_H_
#define EGL_FRONTBUFFER_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include "util/gvr_log.h"
#include "util/gvr_jni.h"

namespace gvr {

typedef void (*PFN_SEC_FRONTBUFFER_SET)(EGLSurface surface, EGLBoolean state);


class FrontBuffer {
private:
    FrontBuffer();

public:
   // static bool tryActivation(JNIEnv * env, bool enable) 
    static bool tryActivation( bool enable) 
	{

        EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
        if (surface == NULL) {
            LOGI("setFrontBuffer surface == NULL: enable=%d",
                    enable);
            return false;
        }

        /* Get the egl extension proc. We are not using JAVA i/f of "setFrontBuffer"
         * as it gives a not necessarily required security exception
         */

        PFN_SEC_FRONTBUFFER_SET egl_SEC_frontbuffer_set =
                (PFN_SEC_FRONTBUFFER_SET) eglGetProcAddress(
                        "EGL_SEC_frontbuffer_set");
        if (egl_SEC_frontbuffer_set != NULL) {
            LOGI("setFrontBuffer surface=%p: enable=%d",
                    surface, enable);
            egl_SEC_frontbuffer_set(surface, enable);
            return true;
        }
        LOGE("This device can't use front buffer surface.");
        return false;
    }

};

}

#endif
