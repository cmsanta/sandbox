/*
 * Example:
 *
 * Render nodes (minimal): Running a compute shader in a window-less
 *                         EGL + GLES 3.1 context.
 *
 * This example shows the minimum code necessary to run an OpenGL (ES) compute
 * shader (aka, a general purpose program on the GPU), on Linux.
 * It uses the DRM render nodes API to gain unprivileged, shared access to the
 * GPU.
 *
 * See <https://en.wikipedia.org/wiki/Direct_Rendering_Manager#Render_nodes> and
 * <https://dri.freedesktop.org/docs/drm/gpu/drm-uapi.html#render-nodes>.
 *
 * Tested on Linux 4.0, Mesa 12.0, Intel GPU (gen7+).
 *
 * Authors:
 *   * Eduardo Lima Mitev <elima@igalia.com>
 *
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * THIS CODE IS PROVIDED AS-IS, WITHOUT WARRANTY OF ANY KIND, OR POSSIBLE
 * LIABILITY TO THE AUTHORS FOR ANY CLAIM OR DAMAGE.
 */

#include <assert.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "utils.h"

static const char gVertexShader[] = "#version 320 es\n"
"layout(location = 0) in vec4 vPosition;\n"
"layout(location = 1) in vec4 vColor;\n"
"out vec4 TriangleColor;\n"
"void main() {\n"
"gl_Position = vPosition;\n"
"TriangleColor = vColor;\n"
"}\n";

static const char gFragmentShader[] = "#version 320 es\n"
"precision mediump float;\n"
"out vec4 fragmentColor;\n"
"in vec4 TriangleColor;\n"
"void main()\n"
"{\n"
"fragmentColor = TriangleColor;\n"
"}\n";

static const char gtextureVS[] = "#version 320 es\n"
    "layout(location = 0) in vec4 position;\n"
    "layout(location = 1) in vec2 texCoords;\n"
    "out vec2 outTexCoords;\n"
    "\nvoid main(void) {\n"
    "    outTexCoords = texCoords;\n"
    "    gl_Position = position;\n"
    "}\n\n";
static const char gtextureFS[] = "#version 320 es\n"
    "precision mediump float;\n\n"
    "in vec2 outTexCoords;\n"
    "out vec4 fragmentColor;\n"
    "uniform sampler2D texture;\n"
    "\nvoid main(void) {\n"
    "    fragmentColor = texture2D(texture, outTexCoords);\n"
    "}\n\n";

GLuint gProgram;
GLuint gTextureProgram;
GLuint gvPositionHandle;
GLuint gvColorAttribHandle;
GLuint gvTexturePositionHandle;
GLuint gvTextureTexCoordsHandle;
GLuint gvTextureSamplerHandle;
GLuint gBufferTexture;
GLuint gFbo;
GLuint intermediateFBO;
// create a color attachment texture
unsigned int screenTexture;

static int setupGraphics(int w, int h, int flip) {
    gProgram = createProgram(gVertexShader, gFragmentShader);
    if (!gProgram) {
        printf("ERROR::Failed to create Program!\n");
        return 0;
    }

    gvPositionHandle = glGetAttribLocation(gProgram, "vPosition");
    gvColorAttribHandle = glGetAttribLocation(gProgram, "vColor");
    checkGlError("glGetAttribLocation");
    printf("glGetAttribLocation(\"vPosition\") = %d\n",
            gvPositionHandle);
    checkGlError("glGetAttribLocation");
    printf("glGetAttribLocation(\"vColor\") = %d\n",
	    gvColorAttribHandle);

    gTextureProgram = createProgram(gtextureVS, gtextureFS);
    if (!gTextureProgram) {
        printf("ERROR::Failed to create Program!\n");
        return 0;
    }

    gvTexturePositionHandle = glGetAttribLocation(gTextureProgram, "position");
    checkGlError("glGetAttribLocation");
    printf("glGetAttribLocation(\"position\") = %d\n",
            gvTexturePositionHandle);
    gvTextureTexCoordsHandle = glGetAttribLocation(gTextureProgram, "texCoords");
    checkGlError("glGetAttribLocation");
    printf("glGetAttribLocation(\"texCoords\") = %d\n",
            gvTextureTexCoordsHandle);
    gvTextureSamplerHandle = glGetUniformLocation(gTextureProgram, "texture");
    checkGlError("glGetUniformLocation");
    printf("glGetUniformLocation(\"texture\") = %d\n",
            gvTextureSamplerHandle);

    glGenFramebuffers(1, &intermediateFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, intermediateFBO);
    if(flip)
     glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_FLIP_Y_EXT, GL_TRUE);
    else
     glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_FLIP_Y_EXT, GL_FALSE);

    // create a color attachment texture
    glGenTextures(1, &screenTexture);
    glBindTexture(GL_TEXTURE_2D, screenTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenTexture, 0);	// we only need a color buffer

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("ERROR::FRAMEBUFFER:: Intermediate framebuffer is not complete!\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, w, h);
    checkGlError("glViewport");
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    checkGlError("glClearColor");
    return 1;
}

#define FLOAT_SIZE_BYTES 4
#define TRIANGLE_VERTICES_DATA_STRIDE_BYTES 5 * FLOAT_SIZE_BYTES

const GLfloat gTriangleVerticesData[] = {
    // X, Y, Z, U, V
    -1.0f, -1.0f, 0, 0.f, 0.f,
    1.0f, -1.0f, 0, 1.f, 0.f,
    -1.0f,  1.0f, 0, 0.f, 1.f,
    1.0f,   1.0f, 0, 1.f, 1.f,
};

static void render_stuff(int width, int height)
{
   static const GLfloat verts[3][2] = {
      {   0.0f,  0.5f },
      {  -0.5f, -0.5f },
      {   0.5f, -0.5f }
   };

   static const GLfloat gTriangleColors[]   = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    checkGlError("glClearColor");
    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    checkGlError("glClear");

    // Bind FBO and draw into it
    glBindFramebuffer(GL_FRAMEBUFFER, /*gFbo*/intermediateFBO);
    checkGlError("glBindFramebuffer");

    glUseProgram(gProgram);
    checkGlError("glUseProgram");

    glVertexAttribPointer(gvPositionHandle, 2, GL_FLOAT, GL_FALSE, 0, verts);
    checkGlError("glVertexAttribPointer");
    glEnableVertexAttribArray(gvPositionHandle);
    checkGlError("glEnableVertexAttribArray");
    glVertexAttribPointer(gvColorAttribHandle, 3, GL_FLOAT, GL_FALSE, 0, gTriangleColors);
    checkGlError("glVertexAttribPointer");
    glEnableVertexAttribArray(gvColorAttribHandle);
    checkGlError("glEnableVertexAttribArray");
    glDrawArrays(GL_TRIANGLES, 0, 3);
    checkGlError("glDrawArrays");

    // Back to the display
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    checkGlError("glBindFramebuffer");

    // Draw copied content on the screen
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(gTextureProgram);
    checkGlError("glUseProgram");

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTexture);

    glVertexAttribPointer(gvTexturePositionHandle, 3, GL_FLOAT, GL_FALSE,
            TRIANGLE_VERTICES_DATA_STRIDE_BYTES, gTriangleVerticesData);
    checkGlError("glVertexAttribPointer");
    glVertexAttribPointer(gvTextureTexCoordsHandle, 2, GL_FLOAT, GL_FALSE,
            TRIANGLE_VERTICES_DATA_STRIDE_BYTES, &gTriangleVerticesData[3]);
    checkGlError("glVertexAttribPointer");
    glEnableVertexAttribArray(gvTexturePositionHandle);
    glEnableVertexAttribArray(gvTextureTexCoordsHandle);
    checkGlError("glEnableVertexAttribArray");
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    checkGlError("glDrawArrays");

   glFinish();

#if 1
   gltWriteTGA("out.tga");
#endif

}

static const char device_name[] = "/dev/dri/card0";

static const EGLint attribs[] = {
   EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
   EGL_RED_SIZE, 8,
   EGL_GREEN_SIZE, 8,
   EGL_BLUE_SIZE, 8,
   EGL_ALPHA_SIZE, 8,
   EGL_SAMPLE_BUFFERS, 1,     // <-- MSAA
   EGL_SAMPLES, 4,            // <-- MSAA
   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
   EGL_NONE
};
static EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };

int32_t main (int32_t argc, char* argv[])
{
   int i;
   int flip = 0;
   bool res;
   EGLContext ctx;
   struct gbm_surface *gs;
   EGLSurface surface;
   uint32_t handle, stride;
   struct kms kms;
   int ret, fd;
   struct gbm_device *gbm;
   struct gbm_bo *bo;
   drmModeCrtcPtr saved_crtc;

   for(i=1; i<argc; i++){
       if(strcmp(argv[i], "-flip") == 0) {
	  flip = 1;
	}
       else{
        printf("Invalid input parameter.\n");
	printf("Only accepted value is -flip\n");
        exit(0);
      }
   }

   fd = open(device_name, O_RDWR|O_CLOEXEC);
   if (fd < 0) {
      /* Probably permissions error */
      printf("couldn't open %s, skipping\n", device_name);
      return -1;
   }

   gbm = gbm_create_device(fd);
   if (gbm == NULL) {
      fprintf(stderr, "couldn't create gbm device\n");
      ret = -1;
      goto close_fd;
   }

   // setup EGL from the GBM device 
   EGLDisplay egl_dpy = eglGetPlatformDisplay (EGL_PLATFORM_GBM_MESA, gbm, NULL);
   //  EGLDisplay egl_dpy = eglGetDisplay (gbm);
   if (egl_dpy == EGL_NO_DISPLAY) {
      printf("eglGetDisplay() failed\n");
      ret = -1;
      goto destroy_gbm_device;
   }

   res = eglInitialize (egl_dpy, NULL, NULL);
   assert (res);

   if (!setup_kms(fd, &kms)) {
      ret = -1;
      goto egl_terminate;
   }

   const char *egl_extension_st = eglQueryString (egl_dpy, EGL_EXTENSIONS);
   assert (strstr (egl_extension_st, "EGL_KHR_create_context") != NULL);
   assert (strstr (egl_extension_st, "EGL_KHR_surfaceless_context") != NULL);

   EGLConfig cfg;
   EGLint count;

   res = eglChooseConfig (egl_dpy, attribs, &cfg, 1, &count);
   assert (res);

   ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, context_attribs);
   if (ctx == NULL) {
      printf("failed to create context\n");
      ret = -1;
      goto egl_terminate;
   }

   res = eglBindAPI (EGL_OPENGL_ES_API);
   assert (res);

   gs = gbm_surface_create(gbm, kms.mode.hdisplay, kms.mode.vdisplay,
			   GBM_BO_FORMAT_ARGB8888,
			   GBM_BO_USE_SCANOUT|GBM_BO_USE_RENDERING);
   surface = eglCreateWindowSurface(egl_dpy, cfg, (EGLNativeWindowType)gs, NULL);

   if (!eglMakeCurrent(egl_dpy, surface, surface, ctx)) {
      printf("failed to make context current\n");
      ret = -1;
      goto destroy_context;
   }

    printGLString("Version", GL_VERSION);
    printGLString("Vendor", GL_VENDOR);
    printGLString("Renderer", GL_RENDERER);
    printGLString("Shading", GL_SHADING_LANGUAGE_VERSION);

    if(!setupGraphics(kms.mode.hdisplay, kms.mode.vdisplay, flip)) {
        printf("Could not set up graphics.\n");
        return 0;
    }

   render_stuff(kms.mode.hdisplay, kms.mode.vdisplay);

   eglSwapBuffers(egl_dpy, surface);

   bo = gbm_surface_lock_front_buffer(gs);
   handle = gbm_bo_get_handle(bo).u32;
   stride = gbm_bo_get_stride(bo);

   ret = drmModeAddFB(fd,
		      kms.mode.hdisplay, kms.mode.vdisplay,
		      24, 32, stride, handle, &kms.fb_id);
   if (ret) {
      fprintf(stderr, "failed to create fb\n");
      goto rm_fb;
   }

   saved_crtc = drmModeGetCrtc(fd, kms.encoder->crtc_id);
   if (saved_crtc == NULL)
      goto rm_fb;

   ret = drmModeSetCrtc(fd, kms.encoder->crtc_id, kms.fb_id, 0, 0,
			&kms.connector->connector_id, 1, &kms.mode);
   if (ret) {
      fprintf(stderr, "failed to set mode: %m\n");
      goto free_saved_crtc;
   }

   getchar();

   ret = drmModeSetCrtc(fd, saved_crtc->crtc_id, saved_crtc->buffer_id,
                        saved_crtc->x, saved_crtc->y,
                        &kms.connector->connector_id, 1, &saved_crtc->mode);
   if (ret) {
      fprintf(stderr, "failed to restore crtc: %m\n");
   }

free_saved_crtc:
   drmModeFreeCrtc(saved_crtc);
rm_fb:
   drmModeRmFB(fd, kms.fb_id);
   eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
destroy_context:
   eglDestroyContext(egl_dpy, ctx);
egl_terminate:
   eglTerminate(egl_dpy);
destroy_gbm_device:
   gbm_device_destroy(gbm);
close_fd:
   close(fd);
   return ret;
}
