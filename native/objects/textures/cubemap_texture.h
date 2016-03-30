/* Copyright 2015 Samsung Electronics Co., LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/***************************************************************************
 * Cube map texture made by six bitmaps.
 ***************************************************************************/

#ifndef CUBEMAP_TEXTURE_H_
#define CUBEMAP_TEXTURE_H_

#include <string>

#if 0
#include <android/bitmap.h>
#endif 
//#define TIZEN_BITMAP

#include "objects/textures/texture.h"
#include "util/gvr_log.h"
#include "util/scope_exit.h"

namespace gvr {
class CubemapTexture: public Texture {
public:

/*    explicit CubemapTexture(JNIEnv* env, jobjectArray bitmapArray,
            int* texture_parameters) :
            Texture(new GLTexture(TARGET, texture_parameters)) {
        pending_gl_task_ = GL_TASK_INIT_BITMAP;
        env_ = env;

        for (int i = 0; i < 6; i++) {
            bitmapRef_[i] = env->NewGlobalRef(env->GetObjectArrayElement(bitmapArray, i));
        }
    }*/

#ifdef TIZEN_BITMAP
//function made for tizen
explicit CubemapTexture(Tizen::Graphics::Bitmap* bitmapArray[],
            int* texture_parameters) :
            Texture(new GLTexture(TARGET, texture_parameters)) {
        pending_gl_task_ = GL_TASK_INIT_BITMAP;
        

        for (int i = 0; i < 6; i++) {
            bitmapRef_[i] =bitmapArray[i];
        }
    }
#endif //TIZEN_BITMAP

/*
   explicit CubemapTexture(JNIEnv* env, GLenum internalFormat,
            GLsizei width, GLsizei height, GLsizei imageSize,
            jobjectArray textureArray, int* textureOffset, int* texture_parameters) :
            Texture(new GLTexture(TARGET, texture_parameters)) {
        pending_gl_task_ = GL_TASK_INIT_INTERNAL_FORMAT;
        env_ = env;
        internalFormat_ = internalFormat;
        width_ = width;
        height_ = height;
        imageSize_ = imageSize;
        memcpy(textureOffset_, textureOffset, 6 * sizeof(textureOffset_[0]));

        for (int i = 0; i < 6; i++) {
            textureRef_[i] = env->NewGlobalRef(env->GetObjectArrayElement(textureArray, i));
        }
    }

*/

//function done for tizen
   explicit CubemapTexture(GLenum internalFormat,
            GLsizei width, GLsizei height, GLsizei imageSize,
            char* textureArray[], int* textureOffset, int* texture_parameters) :
            Texture(new GLTexture(TARGET, texture_parameters)) {
        pending_gl_task_ = GL_TASK_INIT_INTERNAL_FORMAT;
        internalFormat_ = internalFormat;
        width_ = width;
        height_ = height;
        imageSize_ = imageSize;
        memcpy(textureOffset_, textureOffset, 6 * sizeof(textureOffset_[0]));

        for (int i = 0; i < 6; i++) {
            textureRef_[i] = textureArray[i];
        }
    }

    explicit CubemapTexture() :
            Texture(new GLTexture(TARGET)) {
    }

    virtual ~CubemapTexture() {
        // Release global refs. Race condition does not occur because if
        // the runPendingGL is running, the object won't be destructed.
        switch (pending_gl_task_) {
        case GL_TASK_INIT_BITMAP:
        #if 0    
	for (int i = 0; i < 6; i++) {
                env_->DeleteGlobalRef(bitmapRef_[i]);
            }
	#endif
            break;
	
        case GL_TASK_INIT_INTERNAL_FORMAT:
        #if 0    
	for (int i = 0; i < 6; i++) {
                env_->DeleteGlobalRef(textureRef_[i]);
            }
	#endif
            break;
	
        default:
            break;
        }
    }

    GLenum getTarget() const {
        return TARGET;
    }


//function modified 
    virtual void runPendingGL() {
        Texture::runPendingGL();

        switch (pending_gl_task_) {
        case GL_TASK_NONE:
            return;

        case GL_TASK_INIT_BITMAP: {
#ifdef TIZEN_BITMAP
            // Clean up upon scope exit. The SCOPE_EXIT utility is used
            // to avoid duplicated code in the throw case and normal
            // case.
	
            SCOPE_EXIT(
                    pending_gl_task_ = GL_TASK_NONE;
                #if 0    
		for (int i = 0; i < 6; i++) {
                        env_->DeleteGlobalRef(bitmapRef_[i]);
                    }
		#endif
            );

            glBindTexture(TARGET, gl_texture_->id());

            for (int i = 0; i < 6; i++) {
                Tizen::Graphics::Bitmap* bitmap = bitmapRef_[i];

                Tizen::Graphics::BufferInfo info;
                void *pixels;
                int ret;

                if (bitmap == NULL) {
                    std::string error =
                            "new BaseTexture() failed! Input bitmap is NULL.";
                    throw error;
                }
                if ((ret = bitmap->Lock(info)) < 0) {
                    std::string error = "Tizen lock () failed! error = "
                            + ret;
                    throw error;
                }
		#if 0
                if ((ret = AndroidBitmap_lockPixels(env_, bitmap, &pixels)) < 0) {
                    std::string error =
                            "AndroidBitmap_lockPixels () failed! error = " + ret;
                    throw error;
                }
		#endif
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA,
                        info.width, info.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                        info.pPixels);

                bitmap->Unlock();
            }
#endif //TIZEN_BITMAP
            break;
        }

        case GL_TASK_INIT_INTERNAL_FORMAT: {
            // Clean up upon scope exit
            SCOPE_EXIT(
                    pending_gl_task_ = GL_TASK_NONE;
                #if 0    
		for (int i = 0; i < 6; i++) {
                        env_->DeleteGlobalRef(textureRef_[i]);
                    }
		#endif
            );

            glBindTexture(TARGET, gl_texture_->id());
	  	
            for (int i = 0; i < 6; i++) {
               

                char *textureData =textureRef_[i];
                int ret;

                if (textureData == NULL) {
                    std::string error =
                            "new CubemapTexture() failed! Input texture is NULL.";
                    throw error;
                }

                glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
                        internalFormat_, width_, height_, 0, imageSize_, textureData + textureOffset_[i]);

            }

            break;
        }

        } // switch
    }

private:
    CubemapTexture(const CubemapTexture& base_texture);
    CubemapTexture(CubemapTexture&& base_texture);
    CubemapTexture& operator=(const CubemapTexture& base_texture);
    CubemapTexture& operator=(CubemapTexture&& base_texture);

private:
    static const GLenum TARGET = GL_TEXTURE_CUBE_MAP;

    // Enum for pending GL tasks. Keep a comma with each line
    // for easier merging.
    enum {
        GL_TASK_NONE = 0,
        GL_TASK_INIT_BITMAP,
        GL_TASK_INIT_INTERNAL_FORMAT,
    };
    int pending_gl_task_;

    
#ifdef TIZEN_BITMAP
    // For GL_TASK_INIT_BITMAP
    Tizen::Graphics::Bitmap *bitmapRef_[6];
#endif //TIZEN_BITMAP

    // For GL_TASK_INIT_INTERNAL_FORMAT
    GLenum internalFormat_;
    GLsizei width_;
    GLsizei height_;
    GLsizei imageSize_;
    char* textureRef_[6];
    int textureOffset_[6];
};

}
#endif
