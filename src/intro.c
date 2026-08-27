#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

// Particle count must match N in src/shaders/particles.comp.glsl.
#define PARTICLE_COUNT 16384
#define WORKGROUP_SIZE 256

// When the run should end, in seconds: REVEAL_START (30) + REVEAL_EASE (6) +
// the 5-second hold + FADE_OUT_DURATION (4) from particles.vert.glsl's
// MasterFade, i.e. the moment the screen reaches full black, plus a further
// 3-second hold so the ending reads as deliberate rather than the window
// just sitting there. Must be kept in step with those shader constants by
// hand, same as the other timeline values duplicated across files.
#define END_SECONDS 48.f

typedef char GLchar;
typedef int GLsizeiptr;
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPUTE_SHADER 0x91B9
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GL_STATIC_DRAW 0x88E4
#define GL_PROGRAM_POINT_SIZE 0x8642
// Compatibility profiles leave gl_PointCoord undefined unless point sprites are on.
#define GL_POINT_SPRITE 0x8861

#include "shaders.h"
#include "audio.h"

int _fltused = 0;

typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* source, const GLint* length);
typedef void (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
typedef GLint (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void (APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat value);
typedef void (APIENTRYP PFNGLUNIFORM2FPROC)(GLint location, GLfloat x, GLfloat y);
typedef void (APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei count, GLuint* buffers);
typedef void (APIENTRYP PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void (APIENTRYP PFNGLBINDBUFFERBASEPROC)(GLenum target, GLuint index, GLuint buffer);
typedef void (APIENTRYP PFNGLDISPATCHCOMPUTEPROC)(GLuint x, GLuint y, GLuint z);
typedef void (APIENTRYP PFNGLMEMORYBARRIERPROC)(GLbitfield barriers);
#ifdef DEBUG
typedef void (APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum name, GLint* value);
typedef void (APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei max_length, GLsizei* length, GLchar* log);
typedef void (APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum name, GLint* value);
typedef void (APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei max_length, GLsizei* length, GLchar* log);
#endif

// A single NUL-separated name table compresses better than one assignment per
// entry point. The order here must match the enum below.
static const char gl_names[] =
    "glCreateShader\0glShaderSource\0glCompileShader\0glCreateProgram\0glAttachShader\0"
    "glLinkProgram\0glUseProgram\0glGetUniformLocation\0glUniform1f\0glUniform2f\0"
    "glGenBuffers\0glBindBuffer\0glBufferData\0glBindBufferBase\0glDispatchCompute\0glMemoryBarrier\0"
#ifdef DEBUG
    "glGetShaderiv\0glGetShaderInfoLog\0glGetProgramiv\0glGetProgramInfoLog\0"
#endif
    ;

enum {
    GL_FUNCTION_CREATE_SHADER, GL_FUNCTION_SHADER_SOURCE, GL_FUNCTION_COMPILE_SHADER,
    GL_FUNCTION_CREATE_PROGRAM, GL_FUNCTION_ATTACH_SHADER, GL_FUNCTION_LINK_PROGRAM,
    GL_FUNCTION_USE_PROGRAM, GL_FUNCTION_GET_UNIFORM_LOCATION, GL_FUNCTION_UNIFORM_1F,
    GL_FUNCTION_UNIFORM_2F, GL_FUNCTION_GEN_BUFFERS, GL_FUNCTION_BIND_BUFFER,
    GL_FUNCTION_BUFFER_DATA, GL_FUNCTION_BIND_BUFFER_BASE, GL_FUNCTION_DISPATCH_COMPUTE,
    GL_FUNCTION_MEMORY_BARRIER,
#ifdef DEBUG
    GL_FUNCTION_GET_SHADERIV, GL_FUNCTION_GET_SHADER_INFO_LOG,
    GL_FUNCTION_GET_PROGRAMIV, GL_FUNCTION_GET_PROGRAM_INFO_LOG,
#endif
    GL_FUNCTION_COUNT
};

static PROC gl_functions[GL_FUNCTION_COUNT];

#define glCreateShader_ ((PFNGLCREATESHADERPROC)gl_functions[GL_FUNCTION_CREATE_SHADER])
#define glShaderSource_ ((PFNGLSHADERSOURCEPROC)gl_functions[GL_FUNCTION_SHADER_SOURCE])
#define glCompileShader_ ((PFNGLCOMPILESHADERPROC)gl_functions[GL_FUNCTION_COMPILE_SHADER])
#define glCreateProgram_ ((PFNGLCREATEPROGRAMPROC)gl_functions[GL_FUNCTION_CREATE_PROGRAM])
#define glAttachShader_ ((PFNGLATTACHSHADERPROC)gl_functions[GL_FUNCTION_ATTACH_SHADER])
#define glLinkProgram_ ((PFNGLLINKPROGRAMPROC)gl_functions[GL_FUNCTION_LINK_PROGRAM])
#define glUseProgram_ ((PFNGLUSEPROGRAMPROC)gl_functions[GL_FUNCTION_USE_PROGRAM])
#define glGetUniformLocation_ ((PFNGLGETUNIFORMLOCATIONPROC)gl_functions[GL_FUNCTION_GET_UNIFORM_LOCATION])
#define glUniform1f_ ((PFNGLUNIFORM1FPROC)gl_functions[GL_FUNCTION_UNIFORM_1F])
#define glUniform2f_ ((PFNGLUNIFORM2FPROC)gl_functions[GL_FUNCTION_UNIFORM_2F])
#define glGenBuffers_ ((PFNGLGENBUFFERSPROC)gl_functions[GL_FUNCTION_GEN_BUFFERS])
#define glBindBuffer_ ((PFNGLBINDBUFFERPROC)gl_functions[GL_FUNCTION_BIND_BUFFER])
#define glBufferData_ ((PFNGLBUFFERDATAPROC)gl_functions[GL_FUNCTION_BUFFER_DATA])
#define glBindBufferBase_ ((PFNGLBINDBUFFERBASEPROC)gl_functions[GL_FUNCTION_BIND_BUFFER_BASE])
#define glDispatchCompute_ ((PFNGLDISPATCHCOMPUTEPROC)gl_functions[GL_FUNCTION_DISPATCH_COMPUTE])
#define glMemoryBarrier_ ((PFNGLMEMORYBARRIERPROC)gl_functions[GL_FUNCTION_MEMORY_BARRIER])
#ifdef DEBUG
#define glGetShaderiv_ ((PFNGLGETSHADERIVPROC)gl_functions[GL_FUNCTION_GET_SHADERIV])
#define glGetShaderInfoLog_ ((PFNGLGETSHADERINFOLOGPROC)gl_functions[GL_FUNCTION_GET_SHADER_INFO_LOG])
#define glGetProgramiv_ ((PFNGLGETPROGRAMIVPROC)gl_functions[GL_FUNCTION_GET_PROGRAMIV])
#define glGetProgramInfoLog_ ((PFNGLGETPROGRAMINFOLOGPROC)gl_functions[GL_FUNCTION_GET_PROGRAM_INFO_LOG])
#endif

#ifdef DEBUG
// Debug diagnostics go to both the debugger and in4k.log, so shader problems
// are visible without attaching a debugger. Compo builds omit all of this.
static void Report(const char* label, const char* text) {
    DWORD written;
    HANDLE file = CreateFileA("in4k.log", FILE_APPEND_DATA, FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    OutputDebugStringA(label); OutputDebugStringA(text); OutputDebugStringA("\n");
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, label, (DWORD)lstrlenA(label), &written, 0);
    WriteFile(file, text, (DWORD)lstrlenA(text), &written, 0);
    WriteFile(file, "\r\n", 2, &written, 0);
    CloseHandle(file);
}

#ifndef GL_BGR_EXT
#define GL_BGR_EXT 0x80E0
#endif

// "-shot<frames>" runs a fixed 60 Hz timestep and writes shot.bmp at that frame.
// "-live<seconds>" instead uses the real wall-clock timing path and captures
// after that many seconds. Both read the back buffer in-process, so the capture
// is unaffected by window focus or anything else on screen.
// "-steps<k>" runs k simulation steps per rendered frame. Rendering does not
// feed back into the simulation, so this produces exactly the same sequence of
// states as running k times as many frames - it just reaches a late state in a
// fraction of the wall-clock time when tuning.
static int Option(const char* name, int fallback) {
    const char* text = GetCommandLineA();
    int length = lstrlenA(name), index;
    for (; *text; text++) {
        for (index = 0; index < length && text[index] == name[index]; index++) {}
        if (index == length) {
            int value = 0;
            for (text += length; *text >= '0' && *text <= '9'; text++) value = value * 10 + (*text - '0');
            return value ? value : fallback;
        }
    }
    return 0;
}

static void Capture(const char* path, int width, int height) {
    BITMAPFILEHEADER file_header = {0}; BITMAPINFOHEADER info_header = {0};
    int stride = (width * 3 + 3) & ~3, size = stride * height; DWORD written;
    void* pixels = VirtualAlloc(0, size, MEM_COMMIT, PAGE_READWRITE);
    HANDLE file;
    if (!pixels) return;
    glReadPixels(0, 0, width, height, GL_BGR_EXT, GL_UNSIGNED_BYTE, pixels);
    file_header.bfType = 0x4D42; file_header.bfOffBits = 54; file_header.bfSize = 54 + size;
    info_header.biSize = 40; info_header.biWidth = width; info_header.biHeight = height;
    info_header.biPlanes = 1; info_header.biBitCount = 24; info_header.biSizeImage = size;
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, &file_header, 14, &written, 0);
        WriteFile(file, &info_header, 40, &written, 0);
        WriteFile(file, pixels, (DWORD)size, &written, 0);
        CloseHandle(file);
    }
    VirtualFree(pixels, 0, MEM_RELEASE);
}

// Builds "frames/frameNNNNN.bmp" without sprintf, since this project has no
// CRT. Used by -record to number an entire captured sequence for ffmpeg to
// later stitch into a video - reading the back buffer in-process like this
// sidesteps screen/window capture entirely, which is unreliable against this
// exclusive fullscreen window (see -shot/-live above).
static void FormatFrameName(char* buffer, int frame) {
    const char* prefix = "frames/frame";
    int i = 0, d;
    for (; prefix[i]; i++) buffer[i] = prefix[i];
    for (d = 10000; d >= 1; d /= 10) buffer[i++] = '0' + (frame / d) % 10;
    buffer[i++] = '.'; buffer[i++] = 'b'; buffer[i++] = 'm'; buffer[i++] = 'p'; buffer[i] = 0;
}
#endif

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, w_param, l_param);
}

static int LoadOpenGl(void) {
    const char* name = gl_names;
    int index;
    for (index = 0; index < GL_FUNCTION_COUNT; index++) {
        gl_functions[index] = wglGetProcAddress(name);
        if (!gl_functions[index]) return 0;
        while (*name++) {}
    }
    return 1;
}

static void AttachShader(GLuint program, GLenum type, const char* source) {
    GLuint shader = glCreateShader_(type);
    glShaderSource_(shader, 1, &source, 0);
    glCompileShader_(shader);
    glAttachShader_(program, shader);
#ifdef DEBUG
    {
        GLint status; char log[1024]; GLsizei length;
        glGetShaderiv_(shader, GL_COMPILE_STATUS, &status);
        if (!status) { glGetShaderInfoLog_(shader, sizeof(log), &length, log); Report("shader: ", log); }
    }
#endif
}

static void LinkProgram(GLuint program) {
    glLinkProgram_(program);
#ifdef DEBUG
    {
        GLint status; char log[1024]; GLsizei length;
        glGetProgramiv_(program, GL_LINK_STATUS, &status);
        if (!status) { glGetProgramInfoLog_(program, sizeof(log), &length, log); Report("program: ", log); }
    }
#endif
}

static int Run(void) {
    WNDCLASSA window_class = {0}; PIXELFORMATDESCRIPTOR pixel_format = {0}; MSG message;
    HDC device_context; HGLRC render_context; HWND window; int pixel_format_index;
    GLuint simulation, rendering, background, buffer;
    GLint delta_location, simulation_time_location, time_location, resolution_location, background_time_location, background_resolution_location;
    RECT client; int width; int height; DWORD start_ticks; float previous; float now; float delta;
#ifdef DEBUG
    int shot_frames = Option("-shot", 360); int live_seconds = Option("-live", 10); int frame = 0;
    int forced_width = Option("-width", 0), forced_height = Option("-height", 0);
    int steps = Option("-steps", 1), step;
    // "-record<fps>" (e.g. "-record30") captures every frame of a full,
    // deterministic run to frames/frameNNNNN.bmp at that frame rate, for
    // stitching into a video externally - see docs/toolchain.md.
    int record_fps = Option("-record", 30);
    if (record_fps) CreateDirectoryA("frames", 0);
#endif
    window_class.style = CS_OWNDC; window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = GetModuleHandleA(0); window_class.lpszClassName = "in4k.particles";
    if (!RegisterClassA(&window_class)) return 1;
#ifdef DEBUG
    // "-width<w> -height<h>" gives a client area of exactly that size, so the
    // required compo resolutions can be checked without changing display mode.
    if (forced_width && forced_height)
        window = CreateWindowExA(0, window_class.lpszClassName, "", WS_POPUP | WS_VISIBLE, 0, 0, forced_width, forced_height, 0, 0, window_class.hInstance, 0);
    else
#endif
#ifdef COMPO
    window = CreateWindowExA(0, window_class.lpszClassName, "", WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 0, 0, window_class.hInstance, 0);
#else
    window = CreateWindowExA(0, window_class.lpszClassName, "in4k particles", WS_OVERLAPPEDWINDOW | WS_VISIBLE, (GetSystemMetrics(SM_CXSCREEN) - 1280) / 2, (GetSystemMetrics(SM_CYSCREEN) - 720) / 2, 1280, 720, 0, 0, window_class.hInstance, 0);
#endif
    if (!window) return 2;
    device_context = GetDC(window); pixel_format.nSize = sizeof(pixel_format); pixel_format.nVersion = 1;
    pixel_format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pixel_format.iPixelType = PFD_TYPE_RGBA; pixel_format.cColorBits = 32; pixel_format.iLayerType = PFD_MAIN_PLANE;
    pixel_format_index = ChoosePixelFormat(device_context, &pixel_format);
    if (!pixel_format_index || !SetPixelFormat(device_context, pixel_format_index, &pixel_format)) return 3;
    render_context = wglCreateContext(device_context);
    if (!render_context || !wglMakeCurrent(device_context, render_context) || !LoadOpenGl()) return 4;
#ifdef DEBUG
    Report("OpenGL version: ", (const char*)glGetString(GL_VERSION));
#endif

    simulation = glCreateProgram_();
    AttachShader(simulation, GL_COMPUTE_SHADER, particles_compute_shader);
    LinkProgram(simulation);
    rendering = glCreateProgram_();
    AttachShader(rendering, GL_VERTEX_SHADER, particles_vertex_shader);
    AttachShader(rendering, GL_FRAGMENT_SHADER, particles_fragment_shader);
    LinkProgram(rendering);
    background = glCreateProgram_();
    AttachShader(background, GL_VERTEX_SHADER, background_vertex_shader);
    AttachShader(background, GL_FRAGMENT_SHADER, background_fragment_shader);
    LinkProgram(background);
    delta_location = glGetUniformLocation_(simulation, "d");
    simulation_time_location = glGetUniformLocation_(simulation, "t");
    time_location = glGetUniformLocation_(rendering, "t");
    resolution_location = glGetUniformLocation_(rendering, "r");
    background_time_location = glGetUniformLocation_(background, "t");
    background_resolution_location = glGetUniformLocation_(background, "r");

    glGenBuffers_(1, &buffer);
    glBindBuffer_(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData_(GL_SHADER_STORAGE_BUFFER, PARTICLE_COUNT * 32, 0, GL_STATIC_DRAW);
    glBindBufferBase_(GL_SHADER_STORAGE_BUFFER, 0, buffer);

    // Uniforms link to zero, so this first dispatch runs the shader init branch.
    glUseProgram_(simulation);
    glDispatchCompute_(PARTICLE_COUNT / WORKGROUP_SIZE, 1, 1);
    glMemoryBarrier_(GL_SHADER_STORAGE_BARRIER_BIT);

#ifdef DEBUG
    if (steps < 1) steps = 1;
#endif
    AudioStart();
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glEnable(GL_PROGRAM_POINT_SIZE); glEnable(GL_POINT_SPRITE);
    // Subtract ticks as integers before converting: GetTickCount grows to
    // billions on a long-running machine, where float32 can no longer resolve a
    // single frame.
    start_ticks = GetTickCount(); previous = 0.f;
    for (;;) {
        while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) { if (message.message == WM_QUIT) goto done; TranslateMessage(&message); DispatchMessageA(&message); }
        if (GetAsyncKeyState(VK_ESCAPE)) break;
        now = (float)(GetTickCount() - start_ticks) * .001f;
#ifndef DEBUG
        if (now >= END_SECONDS) break;
#endif
        delta = now - previous; previous = now;
        if (delta > 1.f / 30.f) delta = 1.f / 30.f;
        if (delta <= 0.f) delta = .001f;
#ifdef DEBUG
        frame++;
        if (shot_frames) { delta = 1.f / 60.f; now = (float)frame * delta * (float)steps; }
        if (record_fps) { delta = 1.f / (float)record_fps; now = (float)frame * delta; }
#endif

        glUseProgram_(simulation);
        glUniform1f_(simulation_time_location, now);
#ifdef DEBUG
        for (step = 0; step < steps; step++) {
#endif
        glUniform1f_(delta_location, delta);
        glDispatchCompute_(PARTICLE_COUNT / WORKGROUP_SIZE, 1, 1);
        glMemoryBarrier_(GL_SHADER_STORAGE_BARRIER_BIT);
        glUniform1f_(delta_location, -delta);
        glDispatchCompute_(PARTICLE_COUNT / WORKGROUP_SIZE, 1, 1);
        glMemoryBarrier_(GL_SHADER_STORAGE_BARRIER_BIT);
#ifdef DEBUG
        }
#endif

        GetClientRect(window, &client); width = client.right - client.left; height = client.bottom - client.top;
        glViewport(0, 0, width, height);

        // The background triangle covers every pixel of the viewport, so no
        // glClear is needed; blending is off here (opaque replace) and back on
        // for the additive point sprites drawn over it.
        glDisable(GL_BLEND);
        glUseProgram_(background);
        glUniform1f_(background_time_location, now);
        glUniform2f_(background_resolution_location, (float)width, (float)height);
        glBegin(GL_TRIANGLES);
        glVertex2f(-1, -1);
        glVertex2f(3, -1);
        glVertex2f(-1, 3);
        glEnd();
        glEnable(GL_BLEND);

        glUseProgram_(rendering);
        glUniform1f_(time_location, now);
        glUniform2f_(resolution_location, (float)width, (float)height);
        glDrawArrays(GL_POINTS, 0, PARTICLE_COUNT);
#ifdef DEBUG
        // Before the swap: the back buffer is undefined afterwards.
        if (shot_frames && frame >= shot_frames) { Capture("shot.bmp", width, height); break; }
        if (live_seconds && now >= (float)live_seconds) { Capture("shot.bmp", width, height); break; }
        if (record_fps) {
            char path[32];
            FormatFrameName(path, frame);
            Capture(path, width, height);
            if (now >= END_SECONDS) break;
        }
#endif
        SwapBuffers(device_context);
    }
done:
    AudioStop();
    wglMakeCurrent(0, 0); wglDeleteContext(render_context); ReleaseDC(window, device_context); return 0;
}

void __cdecl WinMainCRTStartup(void) { ExitProcess((UINT)Run()); }
