#ifndef GL_H
#define GL_H

#ifdef HAVE_OPENGLES2
#include <GLES2/gl2.h>
#elif __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/glew.h>
#endif

#ifdef VCDEBUG
#define GL(func) \
    do { \
        func; \
        int err = glGetError(); \
        if (err != 0) { \
            printf(#func " failed: %d\n", err); \
        } \
    } while(0)
#else
#define GL(func) func
#endif

struct VCProgram {
    GLuint vertexShader;
    GLuint fragmentShader;
    GLuint program;
};

#endif

