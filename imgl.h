#ifndef IMGL_H
#define IMGL_H

#ifdef HAVE_OPENGLES2
#include <GLES2/gl2.h>
#elif __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/glew.h>
#endif

#if 1
#define GL(func) \
    do { \
        func; \
        int err = glGetError(); \
        if (err != 0) { \
            fprintf(stderr, #func " failed: %d\n", err); \
        } \
    } while(0)
#else
#define GL(func) func
#endif

#endif

