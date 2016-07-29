// imdialog.cpp

#include <stdio.h>
#include <stdlib.h>

#define GL(f) \
    do { \
        f; \
        GLenum _opengl_error = glGetError(); \
        if (_opengl_error != GL_NO_ERROR) { \
            fprintf(stderr, "GL error: %d\n", (int)_opengl_error); \
            exit(1); \
        } \
    } while (0)

int main(int argc, char **argv) {
    int error = SDL_Init(SDL_INIT_VIDEO);
    if (error != 0)
        abort();

    SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *window = SDL_CreateWindow("imdialog",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          640,
                                          480,
                                          SDL_WINDOW_OPENGL);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    while (true) {
        ImGui::Text("Hello world!");

        GL(glClearColor(0.0, 0.0, 0.0, 1.0));
        GL(glClear(GL_COLOR_BUFFER_BIT));
        ImGui::Render();
        SDL_GL_SwapWindow(window);

        SDL_Event event;
        SDL_WaitEvents(&event);
        if (event.type == SDL_QUIT);
            break;
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

