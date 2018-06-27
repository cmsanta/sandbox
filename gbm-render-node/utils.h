 /*
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * THIS CODE IS PROVIDED AS-IS, WITHOUT WARRANTY OF ANY KIND, OR POSSIBLE
 * LIABILITY TO THE AUTHORS FOR ANY CLAIM OR DAMAGE.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <drm.h>
#include <xf86drmMode.h>

struct kms {
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   drmModeModeInfo mode;
   uint32_t fb_id;
};

GLint gltWriteTGA(const char *szFileName);
EGLBoolean setup_kms(int fd, struct kms *kms);
void printGLString(const char *name, GLenum s);
void checkGlError(const char* op);
GLuint loadShader(GLenum shaderType, const char* pSource);
GLuint createProgram(const char* pVertexSource, const char* pFragmentSource);
