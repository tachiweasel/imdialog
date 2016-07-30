// imdialog.cpp

#include "imgui/imgui.h"
#include "imgl.h"
#include <SDL2/SDL.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#endif

#ifdef HAVE_OPENGLES2
#define FRAMEBUFFER_WIDTH   1920
#define FRAMEBUFFER_HEIGHT  1080
#else
#define FRAMEBUFFER_WIDTH   800
#define FRAMEBUFFER_HEIGHT  600
#endif

#define CHARACTER_SCREEN_WIDTH      80
#define CHARACTER_SCREEN_HEIGHT     25

#define WINDOW_WIDTH    50

#define FONT_FILENAME       "Muli.ttf"
#define STANDARD_FONT_SIZE  ((float)FRAMEBUFFER_HEIGHT / 16.6666f)
#define LABEL_FONT_SIZE     ((float)FRAMEBUFFER_HEIGHT / 25.0f)

#define LABEL_COLOR     ImVec4(0.5, 0.5, 0.5, 1.0)

#define MAX_TEXT_SIZE   1024

#define INITIAL_DIRECTORY_ENTRY_CAPACITY    4

#define LIST_HEIGHT 5

#ifndef KDSKBMUTE
#define KDSKBMUTE   0x4B51
#endif
#ifndef KDSKBMODE
#define KDSKBMODE   0x4B45
#endif

struct MenuItem {
    const char *Tag;
    const char *Item;
};

struct InputUI {
    const char *Text;
    char *Data;
};

struct FileUI {
    char *Path;
    int ItemIndex;
};

struct MenuUI {
    const char *Text;
    uint32_t MenuHeight;
    MenuItem *Items;
    size_t ItemCount;
};

enum UIType {
    FileUIType,
    InputUIType,
    MenuUIType,
};

union UITypeData {
    FileUI File;
    InputUI Input;
    MenuUI Menu;
};

struct UI {
    uint32_t Width;
    uint32_t Height;
    UIType Type;
    UITypeData Data;
};

struct UIStatus {
    bool Done;
    int ExitCode;
};

struct ImDialogState {
    GLuint VertexShader;
    GLuint FragmentShader;
    GLuint Program;
    GLuint VBO;
    GLuint IBO;
    GLint APosition;
    GLint ATextureUV;
    GLint AColor;
    GLint UWindowSize;
    GLint UTexture;
    GLuint FontTexture;
    ImFont *standardFont;
    ImFont *labelFont;
};

static ImDialogState g_ImDialogState;

static char *xstrsep(char **stringp, const char* delim) {
    char *start = *stringp;
    char *p = (start != NULL) ? strpbrk(start, delim) : NULL;
    if (p == NULL) {
        *stringp = NULL;
    } else {
        *p = '\0';
        *stringp = p + 1;
    }
    return start;
}

static float ToPixelSize(uint32_t characterSize) {
    return (float)characterSize / (float)CHARACTER_SCREEN_WIDTH * (float)FRAMEBUFFER_WIDTH;
}

static char *GetDataFilePath(const char *filename) {
    char *path = (char *)malloc(PATH_MAX + 1);
    char *dataHome = getenv("XDG_DATA_HOME");
    if (dataHome == NULL || dataHome[0] == '\0') {
        const char *home = getenv("HOME");
        if (home == NULL)
            home = ".";
        snprintf(path, PATH_MAX, "%s/.local/share/imdialog/%s", home, filename);
    } else {
        snprintf(path, PATH_MAX, "%s/imdialog/%s", dataHome, filename);
    }

    struct stat stats;
    if (stat(path, &stats) == 0)
        return path;

    const char *dataDirsLocation = getenv("XDG_DATA_DIRS");
    if (dataDirsLocation == NULL || dataDirsLocation[0] == '\0')
        dataDirsLocation = "/usr/local/share/:/usr/share/";
    char *dataDirs = strdup(dataDirsLocation);
    char *dataDir;
    while ((dataDir = xstrsep(&dataDirs, ":")) != NULL) {
        snprintf(path, PATH_MAX, "%s/imdialog/%s", dataDir, filename);
        if (stat(path, &stats) == 0)
            return path;
    }

    snprintf(path, PATH_MAX, "./%s", filename);
    if (stat(path, &stats) == 0)
        return path;

    fprintf(stderr,
            "error: couldn't find data file `%s`: try installing it to "
            "`~/.local/share/imdialog/%s` or "
            "`/usr/local/share/imdialog/%s`\n",
            filename,
            filename,
            filename);
    exit(1);
}

static void Usage() {
    fprintf(stderr, "usage: imdialog [--fselect|--inputbox|--menu] args...\n");
    exit(EXIT_SUCCESS);
}

static void ParseUint32(uint32_t *value, int *argc, const char ***argv) {
    if (*argc == 0)
        Usage();
    *value = (uint32_t)strtol((*argv)[0], NULL, 0);
    (*argc)--;
    (*argv)++;
}

static void ParseHeightAndWidth(UI *ui, int *argc, const char ***argv) {
    ParseUint32(&ui->Width, argc, argv);
    ParseUint32(&ui->Height, argc, argv);
}

static void ParseFileCommandLine(UI *ui, int *argc, const char ***argv) {
    if (argc == 0)
        Usage();
    ui->Data.File.Path = strdup((*argv)[0]);
    (*argc)--;
    (*argv)++;

    ui->Data.File.ItemIndex = 0;

    ParseHeightAndWidth(ui, argc, argv);

    if (*argc != 0)
        Usage();
}

static void ParseInputCommandLine(UI *ui, int *argc, const char ***argv) {
    if (argc == 0)
        Usage();
    ui->Data.Input.Text = (*argv)[0];
    (*argc)--;
    (*argv)++;

    ParseHeightAndWidth(ui, argc, argv);

    ui->Data.Input.Data = (char *)calloc(MAX_TEXT_SIZE, 1);
    if (*argc > 0) {
        snprintf(ui->Data.Input.Data, MAX_TEXT_SIZE, "%s", (*argv)[0]);
        (*argc)--;
        (*argv)++;
    }

    if (*argc != 0)
        Usage();
}

static void ParseMenuCommandLine(UI *ui, int *argc, const char ***argv) {
    if ((*argc) == 0)
        Usage();
    ui->Data.Menu.Text = (*argv)[0];
    (*argc)--;
    (*argv)++;

    ParseHeightAndWidth(ui, argc, argv);
    ParseUint32(&ui->Data.Menu.MenuHeight, argc, argv);

    uint32_t max_item_count = ((*argc) + 1) / 2;
    ui->Data.Menu.Items = (MenuItem *)malloc(sizeof(MenuItem) * max_item_count);
    while (*argc != 0) {
        if (*argc == 1)
            Usage();

        MenuItem *item = &ui->Data.Menu.Items[ui->Data.Menu.ItemCount];
        item->Tag = (*argv)[0];
        item->Item = (*argv)[1];
        ui->Data.Menu.ItemCount++;

        (*argc) -= 2;
        (*argv) += 2;
    }
}

static UI ParseCommandLine(int argc, const char **argv) {
    UI ui = { 0 };
    argc--;
    argv++;

    if (argc == 0)
        Usage();
    if (strcmp(argv[0], "--fselect") == 0)
        ui.Type = FileUIType;
    else if (strcmp(argv[0], "--inputbox") == 0)
        ui.Type = InputUIType;
    else if (strcmp(argv[0], "--menu") == 0)
        ui.Type = MenuUIType;
    else
        Usage();
    argc--;
    argv++;

    switch (ui.Type) {
    case FileUIType:
        ParseFileCommandLine(&ui, &argc, &argv);
        break;
    case InputUIType:
        ParseInputCommandLine(&ui, &argc, &argv);
        break;
    case MenuUIType:
        ParseMenuCommandLine(&ui, &argc, &argv);
        break;
    }

    return ui;
}

static void ProcessOKCancelButton(UIStatus *status, const char *data) {
    const ImVec2 button_size(ToPixelSize(WINDOW_WIDTH), 0.0);
    if (ImGui::Button("OK", button_size)) {
        status->Done = true;
        status->ExitCode = 0;
        puts(data);
    }
    if (ImGui::Button("Cancel", button_size)) {
        status->Done = true;
        status->ExitCode = 1;
        puts(data);
    }
}

// Caller is responsible for freeing the result.
static char *GetFullPath(const char *path, size_t path_length, const char *filename) {
    size_t filename_length = strlen(path) + 1;
    char *full_path = (char *)malloc(path_length + filename_length + 2);
    snprintf(full_path, path_length + filename_length + 2, "%s/%s", path, filename);
    return full_path;
}

static UIStatus ProcessFileUI(UI *ui) {
    UIStatus status = { false, 0 };
    char *path = strdup(ui->Data.File.Path);
    size_t path_length = strlen(path);
    if (path_length >= 1 && path[path_length - 1] == '/')
        path[path_length - 1] = '\0';

    while (true) {
        struct stat stats = { 0 };
        int error = stat(ui->Data.File.Path, &stats);
        if (error == 0 && (stats.st_mode & S_IFDIR) != 0)
            break;
        char *ptr = strrchr(path, '/');
        if (ptr == NULL) {
            free(path);
            path = strdup("/");
            break;
        }
        *ptr = '\0';
    }

    bool at_root = strcmp(path, "/") == 0;

    char **directory_entries = (char **)malloc(sizeof(char *) * INITIAL_DIRECTORY_ENTRY_CAPACITY);
    size_t directory_entry_count = 0;
    size_t directory_entry_capacity = INITIAL_DIRECTORY_ENTRY_CAPACITY;

    if (!at_root) {
        directory_entries[0] = strdup("Up one level");
        directory_entry_count++;
    }

    DIR *dir = opendir(path);
    if (dir == NULL)
        abort();
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        if (directory_entry_count == directory_entry_capacity) {
            directory_entry_capacity *= 2;
            directory_entries = (char **)realloc(directory_entries,
                                                 sizeof(char *) * directory_entry_capacity);
        }

        char *full_path = GetFullPath(path, path_length, entry->d_name);
        struct stat stats = { 0 };
        stat(full_path, &stats);
        free(full_path);

        size_t filename_length = strlen(entry->d_name);
        directory_entries[directory_entry_count] = (char *)malloc(filename_length + 2);
        snprintf(directory_entries[directory_entry_count],
                 filename_length + 2,
                 "%s%s",
                 entry->d_name,
                 (stats.st_mode & S_IFDIR) ? "/" : "");
        directory_entry_count++;
    }

    ImGui::PushItemWidth(ToPixelSize(WINDOW_WIDTH));
    if (ImGui::ListBox("",
                       &ui->Data.File.ItemIndex,
                       (const char **)directory_entries,
                       directory_entry_count,
                       LIST_HEIGHT)) {
        rewinddir(dir);

        if (!at_root && ui->Data.File.ItemIndex == 0) {
            // The first item is "up one level".
            char *ptr = strrchr(path, '/');
            assert(ptr != NULL);
            *ptr = '\0';

            free(ui->Data.File.Path);
            ui->Data.File.Path = strdup(path);
        } else {
            if (!at_root)
                ui->Data.File.ItemIndex--;

            int index = 0;
            while (index < ui->Data.File.ItemIndex) {
                struct dirent *entry = readdir(dir);
                if (entry->d_name[0] == '.')
                    continue;
                index++;
            }

            struct dirent *entry = NULL;
            do {
                entry = readdir(dir);
            } while (entry != NULL && entry->d_name[0] == '.');
            if (entry == NULL)
                abort();
            
            char *full_path = GetFullPath(path, path_length, entry->d_name);
            struct stat stats = { 0 };
            stat(full_path, &stats);

            if ((stats.st_mode & S_IFDIR) == 0) {
                status.Done = true;
                status.ExitCode = 0;
                puts(full_path);
            } else {
                free(ui->Data.File.Path);
                ui->Data.File.Path = strdup(full_path);
            }

            free(full_path);
        }

        ui->Data.File.ItemIndex = 0;
    }
    ImGui::PopItemWidth();

    closedir(dir);

    const ImVec2 button_size(ToPixelSize(WINDOW_WIDTH), 0.0);
    if (ImGui::Button("Cancel", button_size)) {
        status.Done = true;
        status.ExitCode = 1;
    }

    for (size_t index = 0; index < directory_entry_count; index++)
        free(directory_entries[index]);
    free(directory_entries);
    free(path);

    return status;
}

static UIStatus ProcessInputUI(UI *ui) {
    UIStatus status = { false, 0 };
    const ImVec2 button_size(ToPixelSize(WINDOW_WIDTH), 0.0);
    ImGui::Text(ui->Data.Input.Text);
    ImGui::PushItemWidth(ToPixelSize(WINDOW_WIDTH));
    if (ImGui::InputText("",
                         ui->Data.Input.Data,
                         MAX_TEXT_SIZE,
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        status.Done = true;
        status.ExitCode = 0;
        puts(ui->Data.Input.Data);
    }
    ImGui::PopItemWidth();
    ProcessOKCancelButton(&status, ui->Data.Input.Data);
    return status;
}

static UIStatus ProcessMenuUI(UI *ui) {
    UIStatus status = { false, 0 };
    const ImVec2 button_size(ToPixelSize(WINDOW_WIDTH), 0.0);
    for (size_t item_index = 0; item_index < ui->Data.Menu.ItemCount; item_index++) {
        const MenuItem *item = &ui->Data.Menu.Items[item_index];
        if (ImGui::Selectable(item->Tag, false, 0, button_size)) {
            status.Done = true;
            puts(item->Tag);
        }
        if (item->Item != NULL) {
            ImGui::PushFont(g_ImDialogState.labelFont);
            ImGui::TextColored(LABEL_COLOR, item->Item);
            ImGui::PopFont();
        }
    }
    return status;
}

static UIStatus ProcessUI(UI *ui) {
    switch (ui->Type) {
    case FileUIType:
        return ProcessFileUI(ui);
    case InputUIType:
        return ProcessInputUI(ui);
    case MenuUIType:
        return ProcessMenuUI(ui);
    default:
        assert(0 && "Unknown UI type!");
        abort();
    }
}

static char *Slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    struct stat st;
    if (fstat(fileno(f), &st) < 0) {
        perror("Failed to stat shader");
        abort();
    }
    char *buffer = (char *)malloc(st.st_size + 1);
    if (!buffer)
        abort();
    if (fread(buffer, st.st_size, 1, f) < 1) {
        perror("Failed to read shader");
        abort();
    }
    buffer[st.st_size] = '\0';
    fclose(f);
    return buffer;
}

// Result is null-terminated. Caller is responsible for freeing it. Exits app on failure.
static char *SlurpShaderSource(const char *filename) {
    char *path = (char *)malloc(PATH_MAX + 1);
    char *dataHome = getenv("XDG_DATA_HOME");
    if (dataHome == NULL || dataHome[0] == '\0') {
        const char *home = getenv("HOME");
        if (home == NULL)
            home = ".";
        snprintf(path, PATH_MAX, "%s/.local/share/imdialog/%s", home, filename);
    } else {
        snprintf(path, PATH_MAX, "%s/imdialog/%s", dataHome, filename);
    }

    char *source = Slurp(path);
    if (source != NULL)
        return source;

    const char *dataDirsLocation = getenv("XDG_DATA_DIRS");
    if (dataDirsLocation == NULL || dataDirsLocation[0] == '\0')
        dataDirsLocation = "/usr/local/share/:/usr/share/";
    char *dataDirs = strdup(dataDirsLocation);
    char *dataDir;
    while ((dataDir = xstrsep(&dataDirs, ":")) != NULL) {
        snprintf(path, PATH_MAX, "%s/imdialog/%s", dataDir, filename);
        source = Slurp(path);
        if (source != NULL)
            return source;
    }

    snprintf(path, PATH_MAX, "./%s", filename);
    if ((source = Slurp(path)) != NULL)
        return source;

    fprintf(stderr,
            "video error: couldn't find shader `%s`: try installing it to "
            "`~/.local/share/imdialog/%s` or "
            "`/usr/local/share/imdialog/%s`\n",
            filename,
            filename,
            filename);
    exit(1);
}

static void RenderDrawLists(ImDrawData *draw_data) {
    GL(glViewport(0, 0, FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT));
    GL(glUseProgram(g_ImDialogState.Program));
    GL(glEnable(GL_BLEND));
    GL(glEnable(GL_SCISSOR_TEST));
    GL(glDisable(GL_DEPTH_TEST));
    GL(glBlendEquation(GL_FUNC_ADD));
    GL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    GL(glActiveTexture(GL_TEXTURE0));
    GL(glUniform2f(g_ImDialogState.UWindowSize,
                   (GLfloat)FRAMEBUFFER_WIDTH,
                   (GLfloat)FRAMEBUFFER_HEIGHT));
    GL(glUniform1i(g_ImDialogState.UTexture, 0));

    for (int32_t drawListIndex = 0; drawListIndex < draw_data->CmdListsCount; drawListIndex++) {
        const ImDrawList *draw_list = draw_data->CmdLists[drawListIndex];
        const ImDrawIdx *index_buffer_offset = 0;
        GL(glBindBuffer(GL_ARRAY_BUFFER, g_ImDialogState.VBO));
        GL(glBufferData(GL_ARRAY_BUFFER,
                        (GLsizeiptr)draw_list->VtxBuffer.size() * sizeof(ImDrawVert),
                        (GLvoid *)&draw_list->VtxBuffer.front(),
                        GL_DYNAMIC_DRAW));
        GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ImDialogState.IBO));
        GL(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                        (GLsizeiptr)draw_list->IdxBuffer.size() * sizeof(ImDrawIdx),
                        (GLvoid *)&draw_list->IdxBuffer.front(),
                        GL_DYNAMIC_DRAW));

        for (const ImDrawCmd *draw_command = draw_list->CmdBuffer.begin();
             draw_command != draw_list->CmdBuffer.end();
             draw_command++) {
            if (draw_command->UserCallback != NULL) {
                draw_command->UserCallback(draw_list, draw_command);
                continue;
            }
            GL(glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)draw_command->TextureId));
            GL(glScissor((int)draw_command->ClipRect.x,
                         (int)(FRAMEBUFFER_HEIGHT - draw_command->ClipRect.w),
                         (int)(draw_command->ClipRect.z - draw_command->ClipRect.x),
                         (int)(draw_command->ClipRect.w - draw_command->ClipRect.y)));
            GL(glDrawElements(GL_TRIANGLES,
                              (GLsizei)draw_command->ElemCount,
                              (sizeof(ImDrawIdx) == 2) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                              index_buffer_offset));
            index_buffer_offset += draw_command->ElemCount;
        }
    }
}

static GLuint CompileShaderFromCString(GLint shader_type, const char *source) {
    GLuint shader = glCreateShader(shader_type);
    GL(glShaderSource(shader, 1, &source, NULL));
    GL(glCompileShader(shader));
#ifdef IMDEBUG
    GLint compile_status = 0;
    GL(glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status));
    if (compile_status != GL_TRUE) {
        fprintf(stderr, "Failed to compile shader!\n");
        GLint infoLogLength = 0;
        GL(glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength));
        char *infoLog = (char *)malloc(infoLogLength + 1);
        GL(glGetShaderInfoLog(shader, (GLsizei)infoLogLength, NULL, infoLog));
        infoLog[infoLogLength] = '\0';
        fprintf(stderr, "%s\n", infoLog);
        abort();
    }
#endif
    return shader;
}

static GLuint CompileShader(GLint shader_type, const char *filename) {
    char *source = SlurpShaderSource(filename);
    return CompileShaderFromCString(shader_type, source);
}

static GLuint CreateProgram(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    GL(glAttachShader(program, vertex_shader));
    GL(glAttachShader(program, fragment_shader));
    return program;
}

static void CreateDialogState() {
    ImGuiIO &io = ImGui::GetIO();
    uint8_t *pixels = NULL;
    int width = 0, height = 0;
    char *ui_font_path = GetDataFilePath(FONT_FILENAME);
    g_ImDialogState.standardFont = io.Fonts->AddFontFromFileTTF(ui_font_path,
                                                                STANDARD_FONT_SIZE,
                                                                NULL);
    g_ImDialogState.labelFont = io.Fonts->AddFontFromFileTTF(ui_font_path, LABEL_FONT_SIZE, NULL);
    free(ui_font_path);

    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    glGenTextures(1, &g_ImDialogState.FontTexture);
    GL(glBindTexture(GL_TEXTURE_2D, g_ImDialogState.FontTexture));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL(glTexImage2D(GL_TEXTURE_2D,
                    0,
                    GL_RGBA,
                    width,
                    height,
                    0,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    pixels));
    io.Fonts->TexID = (void *)(uintptr_t)g_ImDialogState.FontTexture;
    GL(glBindTexture(GL_TEXTURE_2D, 0));

    g_ImDialogState.VertexShader = CompileShader(GL_VERTEX_SHADER, "imgui.vs.glsl");
    g_ImDialogState.FragmentShader = CompileShader(GL_FRAGMENT_SHADER, "imgui.fs.glsl");
    g_ImDialogState.Program = CreateProgram(g_ImDialogState.VertexShader,
                                            g_ImDialogState.FragmentShader);
    GL(glLinkProgram(g_ImDialogState.Program));

    g_ImDialogState.UWindowSize = glGetUniformLocation(g_ImDialogState.Program, "uWindowSize");
    g_ImDialogState.UTexture = glGetUniformLocation(g_ImDialogState.Program, "uTexture");
    g_ImDialogState.APosition = glGetAttribLocation(g_ImDialogState.Program, "aPosition");
    g_ImDialogState.ATextureUV = glGetAttribLocation(g_ImDialogState.Program, "aTextureUV");
    g_ImDialogState.AColor = glGetAttribLocation(g_ImDialogState.Program, "aColor");
    GL(glUseProgram(g_ImDialogState.Program));

    glGenBuffers(1, &g_ImDialogState.VBO);
    GL(glBindBuffer(GL_ARRAY_BUFFER, g_ImDialogState.VBO));
    GL(glVertexAttribPointer(g_ImDialogState.APosition,
                             2,
                             GL_FLOAT,
                             GL_FALSE,
                             sizeof(ImDrawVert),
                             (const GLvoid *)offsetof(ImDrawVert, pos)));
    GL(glVertexAttribPointer(g_ImDialogState.ATextureUV,
                             2,
                             GL_FLOAT,
                             GL_FALSE,
                             sizeof(ImDrawVert),
                             (const GLvoid *)offsetof(ImDrawVert, uv)));
    GL(glVertexAttribPointer(g_ImDialogState.AColor,
                             4,
                             GL_UNSIGNED_BYTE,
                             GL_TRUE,
                             sizeof(ImDrawVert),
                             (const GLvoid *)offsetof(ImDrawVert, col)));

    GL(glEnableVertexAttribArray(g_ImDialogState.APosition));
    GL(glEnableVertexAttribArray(g_ImDialogState.ATextureUV));
    GL(glEnableVertexAttribArray(g_ImDialogState.AColor));

    glGenBuffers(1, &g_ImDialogState.IBO);
}

static void InitKeys() {
    ImGuiIO &io = ImGui::GetIO();
    io.KeyMap[ImGuiKey_Tab] = SDLK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
    io.KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
    io.KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
    io.KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
    io.KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
    io.KeyMap[ImGuiKey_Delete] = SDLK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = SDLK_BACKSPACE;
    io.KeyMap[ImGuiKey_Enter] = SDLK_RETURN;
    io.KeyMap[ImGuiKey_Escape] = SDLK_ESCAPE;
    io.KeyMap[ImGuiKey_A] = SDLK_a;
    io.KeyMap[ImGuiKey_C] = SDLK_c;
    io.KeyMap[ImGuiKey_V] = SDLK_v;
    io.KeyMap[ImGuiKey_X] = SDLK_x;
    io.KeyMap[ImGuiKey_Y] = SDLK_y;
    io.KeyMap[ImGuiKey_Z] = SDLK_z;
}

extern "C" int main(int argc, char **argv) {
    UI ui = ParseCommandLine(argc, (const char **)argv);

    int error = SDL_Init(SDL_INIT_VIDEO);
    if (error != 0)
        abort();

    int value = 1;
    SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &value);

    SDL_Window *window = SDL_CreateWindow("imdialog",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          FRAMEBUFFER_WIDTH,
                                          FRAMEBUFFER_HEIGHT,
                                          SDL_WINDOW_OPENGL);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_ShowCursor(0);

#if !defined(HAVE_OPENGLES2) && !defined(__APPLE__)
    int glewError = glewInit();
    if (glewError != GLEW_OK) {
        fprintf(stderr, "Failed to initialize GLEW: %d!\n", (int)glewError);
        abort();
    }
#endif

    CreateDialogState();
    InitKeys();

    ImGuiIO &io = ImGui::GetIO();
    io.RenderDrawListsFn = RenderDrawLists;
    io.DisplaySize = ImVec2((float)FRAMEBUFFER_WIDTH, (float)FRAMEBUFFER_HEIGHT);
    io.DisplayFramebufferScale = ImVec2(1.0, 1.0);
    io.DeltaTime = 1.0f / 60.0f;

    UIStatus status;
    bool done = false;
    while (!done) {
        ImGui::NewFrame();
        bool show_by_default = true;
        ImGui::SetNextWindowPosCenter();
        ImGui::Begin("imdialog",
                     &show_by_default,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
        if (!ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive())
            ImGui::SetKeyboardFocusHere();
        status = ProcessUI(&ui);
        ImGui::End();

        GL(glClearColor(0.0, 0.0, 0.0, 1.0));
        GL(glClear(GL_COLOR_BUFFER_BIT));
        ImGui::Render();
        SDL_GL_SwapWindow(window);

        if (status.Done) {
            done = true;
            break;
        }

        SDL_Event event;
        int key;
        SDL_WaitEvent(&event);
        if (event.type == SDL_QUIT)
            break;
        switch (event.type) {
        case SDL_QUIT:
            done = true;
            break;
        case SDL_TEXTINPUT:
            io.AddInputCharactersUTF8(event.text.text);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            key = event.key.keysym.sym & ~SDLK_SCANCODE_MASK;
            io.KeysDown[key] = (event.type == SDL_KEYDOWN);
            io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
            io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
            io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
            io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);
            if (key == SDLK_ESCAPE)
                done = true;
            break;
        }

        int mouse_x = -1, mouse_y = -1;
        uint32_t mouse_mask = 0;
        io.MouseDrawCursor = (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
        if (io.MouseDrawCursor)
            mouse_mask = SDL_GetMouseState(&mouse_x, &mouse_y);
        io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);
        io.MouseDown[0] = (mouse_mask & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        io.MouseDown[1] = (mouse_mask & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
        io.MouseDown[2] = (mouse_mask & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

#ifdef __linux__
    int tty = fileno(stdin);
    if (isatty(tty)) {
        if (ioctl(tty, KDSKBMUTE, 0) != 0)
            ioctl(tty, KDSKBMODE, K_XLATE);
    }
#endif

    return status.ExitCode;
}

