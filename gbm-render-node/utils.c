 /*
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * THIS CODE IS PROVIDED AS-IS, WITHOUT WARRANTY OF ANY KIND, OR POSSIBLE
 * LIABILITY TO THE AUTHORS FOR ANY CLAIM OR DAMAGE.
 */

#include "utils.h"


// WriteTGA.c
// Captures the current screen/window from the OpenGL context
// and writes the pixels out as a targa formatted file.
// Richard S. Wright Jr.
// OpenGL SuperBible
// Define targa header.
#pragma pack(1)
typedef struct
    {
    GLbyte	identsize;              // Size of ID field that follows header (0)
    GLbyte	colorMapType;           // 0 = None, 1 = paletted
    GLbyte	imageType;              // 0 = none, 1 = indexed, 2 = rgb, 3 = grey, +8=rle
    unsigned short	colorMapStart;  // First colour map entry
    unsigned short	colorMapLength; // Number of colors
    unsigned char 	colorMapBits;   // bits per palette entry
    unsigned short	xstart;         // image x origin
    unsigned short	ystart;         // image y origin
    unsigned short	width;          // width in pixels
    unsigned short	height;         // height in pixels
    GLbyte	bits;                   // bits per pixel (8 16, 24, 32)
    GLbyte	descriptor;             // image descriptor
    } TGAHEADER;
#pragma pack(8)

 ////////////////////////////////////////////////////////////////////
// Capture the current viewport and save it as a targa file.
// Be sure and call SwapBuffers for double buffered contexts or
// glFinish for single buffered contexts before calling this function.
// Returns 0 if an error occurs, or 1 on success.
GLint gltWriteTGA(const char *szFileName)
    {
    FILE *pFile;                // File pointer
    TGAHEADER tgaHeader;		// TGA file header
    unsigned long lImageSize;   // Size in bytes of image
    GLbyte	*pBits = NULL;      // Pointer to bits
    GLint iViewport[4];         // Viewport in pixels
    GLint lastBuffer;          // Storage for the current read buffer setting
    GLint i;
    GLbyte skip;
    
	// Get the viewport dimensions
	glGetIntegerv(GL_VIEWPORT, iViewport);

    // How big is the image going to be (targas are tightly packed)
	lImageSize = iViewport[2] * 4 * iViewport[3];	

    // Allocate block. If this doesn't work, go home
    pBits = (GLbyte *)malloc(lImageSize);
    if(pBits == NULL)
        return 0;

    // Read bits from color buffer
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_SKIP_ROWS, 0);
	glPixelStorei(GL_PACK_SKIP_PIXELS, 0);

    // Get the current read buffer setting and save it. Switch to
    // the front buffer and do the read operation. Finally, restore
    // the read buffer state
    glGetIntegerv(GL_READ_BUFFER, &lastBuffer);
    glReadBuffer(GL_BACK);

    glReadPixels(0, 0, iViewport[2], iViewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pBits);
    glReadBuffer(lastBuffer);
    
    // Initialize the Targa header
    tgaHeader.identsize = 0;
    tgaHeader.colorMapType = 0;
    tgaHeader.imageType = 2;
    tgaHeader.colorMapStart = 0;
    tgaHeader.colorMapLength = 0;
    tgaHeader.colorMapBits = 0;
    tgaHeader.xstart = 0;
    tgaHeader.ystart = 0;
    tgaHeader.width = iViewport[2];
    tgaHeader.height = iViewport[3];
    tgaHeader.bits = 32;
    tgaHeader.descriptor = 0;
    
    // Do byte swap for big vs little endian
#ifdef __APPLE__
    BYTE_SWAP(tgaHeader.colorMapStart);
    BYTE_SWAP(tgaHeader.colorMapLength);
    BYTE_SWAP(tgaHeader.xstart);
    BYTE_SWAP(tgaHeader.ystart);
    BYTE_SWAP(tgaHeader.width);
    BYTE_SWAP(tgaHeader.height);
#endif

    // Attempt to open the file
    pFile = fopen(szFileName, "wb");
    if(pFile == NULL)
        {
        free(pBits);    // Free buffer and return error
        return 0;
        }
     
    // Write the header
    fwrite(&tgaHeader, sizeof(TGAHEADER), 1, pFile);

    //convert RGB(a) to BGR(a)
    for(i=0; i<lImageSize; i+=4){
        skip = pBits[i];
        pBits[i] = pBits[i+2];
        pBits[i+2] = skip;
    }
    
    // Write the image data
    fwrite(pBits, lImageSize, 1, pFile);
              
    // Free temporary buffer and close the file
    free(pBits);    
    fclose(pFile);
    
    // Success!
    return 1;
}

void printGLString(const char *name, GLenum s) {
    const char *v = (const char *) glGetString(s);
    fprintf(stderr, "GL %s = %s\n", name, v);
}

void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        printf("after %s() glError (0x%x)\n", op, error);
    }
}

EGLBoolean setup_kms(int fd, struct kms *kms)
{
   drmModeRes *resources;
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   int i;

   resources = drmModeGetResources(fd);
   if (!resources) {
      fprintf(stderr, "drmModeGetResources failed\n");
      return EGL_FALSE;
   }

   for (i = 0; i < resources->count_connectors; i++) {
      connector = drmModeGetConnector(fd, resources->connectors[i]);
      if (connector == NULL)
	 continue;

      if (connector->connection == DRM_MODE_CONNECTED &&
	  connector->count_modes > 0)
	 break;

      drmModeFreeConnector(connector);
   }

   if (i == resources->count_connectors) {
      fprintf(stderr, "No currently active connector found.\n");
      return EGL_FALSE;
   }

   for (i = 0; i < resources->count_encoders; i++) {
      encoder = drmModeGetEncoder(fd, resources->encoders[i]);

      if (encoder == NULL)
	 continue;

      if (encoder->encoder_id == connector->encoder_id)
	 break;

      drmModeFreeEncoder(encoder);
   }

   kms->connector = connector;
   kms->encoder = encoder;
   kms->mode = connector->modes[0];

   return EGL_TRUE;
}

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    fprintf(stderr, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }
    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }
    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    fprintf(stderr, "Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}
