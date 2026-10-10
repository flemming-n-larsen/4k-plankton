#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

// Particle count must match N in src/shaders/particles.comp.glsl.
#define PARTICLE_COUNT 16384
#define WORKGROUP_SIZE 256

// When the run should end, in seconds: the word forms over seconds 30 to 36,
// holds for 9, and the master fade in particles.vert.glsl and
// background.frag.glsl takes the screen to full black over seconds 45 to 53;
// a further 3-second hold makes the ending read as deliberate rather than the
// window just sitting there. Must be kept in step with those shader constants
// by hand, same as the other timeline values duplicated across files.
#define END_SECONDS 59.f

// The simulation runs at a fixed 1/60 s step so the run is reproducible on any
// machine; see the integration loop in Run(). MAX_CATCHUP bounds how many steps
// one frame may run when the renderer falls behind, so a slow machine cannot
// stall in an ever-growing catch-up loop - it plays slightly slow instead.
#define SIM_STEP (1.f / 60.f)
#ifdef DEBUG
#define MAX_CATCHUP 64
#else
#define MAX_CATCHUP 4
#endif

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

// Uniform locations, fixed by layout(location = N) in the shaders, so none is
// looked up by name: t is 0 in all three programs, d (compute) and r (vertex
// and background) are 1.
#define UNIFORM_TIME 0
#define UNIFORM_DELTA 1
#define UNIFORM_RESOLUTION 1

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
typedef void (APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat value);
typedef void (APIENTRYP PFNGLUNIFORM2FPROC)(GLint location, GLfloat x, GLfloat y);
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
    "glLinkProgram\0glUseProgram\0glUniform1f\0glUniform2f\0"
    "glBindBuffer\0glBufferData\0glBindBufferBase\0glDispatchCompute\0glMemoryBarrier\0"
#ifdef DEBUG
    "glGetShaderiv\0glGetShaderInfoLog\0glGetProgramiv\0glGetProgramInfoLog\0"
#endif
    ;

enum {
    GL_FUNCTION_CREATE_SHADER, GL_FUNCTION_SHADER_SOURCE, GL_FUNCTION_COMPILE_SHADER,
    GL_FUNCTION_CREATE_PROGRAM, GL_FUNCTION_ATTACH_SHADER, GL_FUNCTION_LINK_PROGRAM,
    GL_FUNCTION_USE_PROGRAM, GL_FUNCTION_UNIFORM_1F,
    GL_FUNCTION_UNIFORM_2F, GL_FUNCTION_BIND_BUFFER,
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
#define glUniform1f_ ((PFNGLUNIFORM1FPROC)gl_functions[GL_FUNCTION_UNIFORM_1F])
#define glUniform2f_ ((PFNGLUNIFORM2FPROC)gl_functions[GL_FUNCTION_UNIFORM_2F])
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
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    OutputDebugStringA(label); OutputDebugStringA(text); OutputDebugStringA("\n");
    // Run() attaches to the parent console, so the same lines show in the
    // PowerShell window the Debug build was started from.
    if (console && console != INVALID_HANDLE_VALUE) {
        WriteFile(console, label, (DWORD)lstrlenA(label), &written, 0);
        WriteFile(console, text, (DWORD)lstrlenA(text), &written, 0);
        WriteFile(console, "\r\n", 2, &written, 0);
    }
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, label, (DWORD)lstrlenA(label), &written, 0);
    WriteFile(file, text, (DWORD)lstrlenA(text), &written, 0);
    WriteFile(file, "\r\n", 2, &written, 0);
    CloseHandle(file);
}

// Appends " name=value" to the text at position; there is no CRT to format it.
static char* AppendNumber(char* position, const char* name, int value) {
    char digits[12]; int count = 0, index;
    *position++ = ' ';
    for (; *name; name++) *position++ = *name;
    *position++ = '=';
    if (value < 0) { *position++ = '-'; value = -value; }
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    for (index = count - 1; index >= 0; index--) *position++ = digits[index];
    *position = 0;
    return position;
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

static int LoadOpenGl(void) {
    const char* name = gl_names;
    int index;
    for (index = 0; index < GL_FUNCTION_COUNT; index++) {
        gl_functions[index] = wglGetProcAddress(name);
#ifdef DEBUG
        if (!gl_functions[index]) return 0;
#endif
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

// An initialised static rather than a local filled in field by field: the
// stores cost more than the mostly-zero data. PFD_TYPE_RGBA and PFD_MAIN_PLANE
// are both 0.
static PIXELFORMATDESCRIPTOR pixel_format = {
    sizeof(PIXELFORMATDESCRIPTOR), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32
};

// In the Compo build the main function is itself the entry point and ends the
// process: a wrapper calling it would cost bytes.
#ifdef DEBUG
static int Run(void) {
#else
void __cdecl WinMainCRTStartup(void) {
#endif
    HDC device_context; HWND window; MSG message;
    GLuint simulation, rendering, background;
    int width; int height; float now; float sim_time; int sim_steps; int step;
#ifdef DEBUG
    RECT client; HGLRC render_context; int pixel_format_index;
#endif
#ifdef DEBUG
    int shot_frames = Option("-shot", 360); int live_seconds = Option("-live", 10); int frame = 0;
    int forced_width = Option("-width", 0), forced_height = Option("-height", 0);
    int steps = Option("-steps", 1);
    // Timing log: when the sound device really starts, and how far the
    // picture's sim time lags the sound's position, which is its clock.
    DWORD run_ticks = GetTickCount(), audio_ticks = 0;
    int first_frame_logged = 0, probe_started = 0, next_log_ms = 0;
    // "-record<fps>" (e.g. "-record30") captures every frame of a full,
    // deterministic run to frames/frameNNNNN.bmp at that frame rate, for
    // stitching into a video externally - see docs/toolchain.md.
    int record_fps = Option("-record", 30);
    if (record_fps) CreateDirectoryA("frames", 0);
    AttachConsole((DWORD)-1);
#endif
    // The predefined "static" class rather than one of our own: it already has
    // a window procedure, which saves registering a class and writing one. The
    // intro never handles a message - it ends on Escape or on its own clock -
    // so nothing is lost but the close button.
#ifdef DEBUG
    // "-width<w> -height<h>" gives a client area of exactly that size, so the
    // required compo resolutions can be checked without changing display mode.
    if (forced_width && forced_height)
        window = CreateWindowExA(0, "static", 0, WS_POPUP | WS_VISIBLE, 0, 0, forced_width, forced_height, 0, 0, 0, 0);
    else
#endif
#ifdef COMPO
    // The client area of a WS_POPUP window is the whole window, so the window
    // size is also the framebuffer size - no GetClientRect needed. The size is
    // the desktop's, unless the build fixes it with COMPO_WIDTH/COMPO_HEIGHT
    // (the per-resolution compo executables).
#ifdef COMPO_WIDTH
    window = CreateWindowExA(WS_EX_TOPMOST, "static", 0, WS_POPUP | WS_VISIBLE, 0, 0,
                             width = COMPO_WIDTH, height = COMPO_HEIGHT, 0, 0, 0, 0);
#else
    window = CreateWindowExA(WS_EX_TOPMOST, "static", 0, WS_POPUP | WS_VISIBLE, 0, 0,
                             width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN), 0, 0, 0, 0);
#endif
#else
    window = CreateWindowExA(0, "static", "in4k particles", WS_OVERLAPPEDWINDOW | WS_VISIBLE, (GetSystemMetrics(SM_CXSCREEN) - 1280) / 2, (GetSystemMetrics(SM_CYSCREEN) - 720) / 2, 1280, 720, 0, 0, 0, 0);
#endif
#ifdef DEBUG
    if (!window) return 2;
#endif
#ifdef COMPO
    // The window covers the whole screen, so the arrow would sit on top of the
    // picture for the full run.
    ShowCursor(0);
#endif
    device_context = GetDC(window);
#ifdef DEBUG
    pixel_format_index = ChoosePixelFormat(device_context, &pixel_format);
    if (!pixel_format_index || !SetPixelFormat(device_context, pixel_format_index, &pixel_format)) return 3;
    render_context = wglCreateContext(device_context);
    if (!render_context || !wglMakeCurrent(device_context, render_context) || !LoadOpenGl()) return 4;
#else
    // No error checks in the Compo build: without a window or an OpenGL 4.3
    // context there is nothing to show, and the checks only cost bytes.
    SetPixelFormat(device_context, ChoosePixelFormat(device_context, &pixel_format), &pixel_format);
    wglMakeCurrent(device_context, wglCreateContext(device_context));
    LoadOpenGl();
#endif
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
    // No vertex stage: in a compatibility profile a program with only a
    // fragment shader runs the fixed-function vertex pipeline, which is exactly
    // what a "gl_Position = gl_Vertex" pass-through would have done.
    background = glCreateProgram_();
    AttachShader(background, GL_FRAGMENT_SHADER, background_fragment_shader);
    LinkProgram(background);

#ifndef COMPO
    GetClientRect(window, &client); width = client.right - client.left; height = client.bottom - client.top;
#endif
    // The viewport already covers the whole window when the context is first
    // made current, and neither the window nor the resolution ever changes, so
    // the resolution uniforms are set once here rather than every frame.
    glUseProgram_(rendering);
    glUniform2f_(UNIFORM_RESOLUTION, (float)width, (float)height);
    glUseProgram_(background);
    glUniform2f_(UNIFORM_RESOLUTION, (float)width, (float)height);

    // Buffer name 1 is used without glGenBuffers: the compatibility profile
    // creates an object for any unused name when it is first bound.
    glBindBuffer_(GL_SHADER_STORAGE_BUFFER, 1);
    glBufferData_(GL_SHADER_STORAGE_BUFFER, PARTICLE_COUNT * 32, 0, GL_STATIC_DRAW);
    glBindBufferBase_(GL_SHADER_STORAGE_BUFFER, 0, 1);

    // Uniforms link to zero, so this first dispatch runs the shader init branch.
    glUseProgram_(simulation);
    glDispatchCompute_(PARTICLE_COUNT / WORKGROUP_SIZE, 1, 1);
    glMemoryBarrier_(GL_SHADER_STORAGE_BARRIER_BIT);

#ifdef DEBUG
    if (steps < 1) steps = 1;
#endif
    AudioStart();
#ifdef DEBUG
    {
        char text[96];
        audio_ticks = GetTickCount();
        text[0] = 0;
        AppendNumber(text, "since_run_ms", (int)(audio_ticks - run_ticks));
        Report("audio started (waveOut written):", text);
    }
#endif
    // The destination is scaled by the source's alpha: the background writes
    // alpha 0 and so replaces what was there, the point sprites write 1 and
    // add to it. Blending never has to be switched off and on again per frame.
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_SRC_ALPHA); glEnable(GL_PROGRAM_POINT_SIZE); glEnable(GL_POINT_SPRITE);
    sim_steps = 0; sim_time = 0.f;
    for (;;) {
#ifdef DEBUG
        while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&message);
#else
        // One message a frame, also in the Compo build: a window whose thread
        // takes none for five seconds is marked as hung, and Windows may then
        // put a frozen "Not responding" copy over the picture.
        PeekMessageA(&message, 0, 0, 0, PM_REMOVE);
#endif
        if (GetAsyncKeyState(VK_ESCAPE)) break;
        // The sound's position is the only clock. The picture starts when the
        // sound does and then follows it sample for sample, so the two cannot
        // drift apart whatever the device's start-up delay or clock rate.
        now = (float)AudioSamples() * (1.f / AUDIO_SAMPLE_RATE);
#ifndef DEBUG
        if (sim_time >= END_SECONDS) break;
#endif
#ifdef DEBUG
        frame++;
        if (shot_frames) now = (float)frame * (float)steps * SIM_STEP;
        if (record_fps) now = (float)frame / (float)record_fps;
#endif

        // Fixed timestep, not the wall-clock frame delta. Particle Life is
        // chaotic, so integrating with whatever delta a machine happened to
        // produce made the cluster evolve differently on every refresh rate -
        // same binary, different-looking intro. Stepping a constant SIM_STEP
        // makes the number of steps taken by a given wall-clock time exactly
        // floor(now / SIM_STEP) everywhere, so the run is reproducible. The
        // count is kept as an integer and sim_time derived from it, rather than
        // accumulating sim_time by repeated addition: the accumulated float
        // drifts against a multiplied time, which flips the comparison below at
        // the boundary and costs a step.
        // sim_time, not the wall clock, drives every shader uniform below, so
        // a machine that cannot keep up plays the same intro slightly slowly
        // rather than a different one.
        glUseProgram_(simulation);
        for (step = 0; step < MAX_CATCHUP && (float)(sim_steps + 1) * SIM_STEP <= now; step++) {
            sim_time = (float)++sim_steps * SIM_STEP;
            glUniform1f_(UNIFORM_TIME, sim_time);
            glUniform1f_(UNIFORM_DELTA, SIM_STEP);
            glDispatchCompute_(PARTICLE_COUNT / WORKGROUP_SIZE, 1, 1);
            glMemoryBarrier_(GL_SHADER_STORAGE_BARRIER_BIT);
            glUniform1f_(UNIFORM_DELTA, -SIM_STEP);
            glDispatchCompute_(PARTICLE_COUNT / WORKGROUP_SIZE, 1, 1);
            glMemoryBarrier_(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        // glRecti covers every pixel of the viewport in one call, so no glClear
        // is needed and no vertex data has to be handed over; its alpha of 0
        // makes it replace the last frame (see glBlendFunc above).
        glUseProgram_(background);
        glUniform1f_(UNIFORM_TIME, sim_time);
        glRecti(-1, -1, 1, 1);

        glUseProgram_(rendering);
        glUniform1f_(UNIFORM_TIME, sim_time);
        glDrawArrays(GL_POINTS, 0, PARTICLE_COUNT);
#ifdef DEBUG
        // Before the swap: the back buffer is undefined afterwards.
        if (shot_frames && frame >= shot_frames) { Capture("shot.bmp", width, height); break; }
        if (live_seconds && sim_time >= (float)live_seconds) { Capture("shot.bmp", width, height); break; }
        if (record_fps) {
            char path[32];
            FormatFrameName(path, frame);
            Capture(path, width, height);
            if (sim_time >= END_SECONDS) break;
        }
#endif
        SwapBuffers(device_context);
#ifdef DEBUG
        {
            char text[96], *end;
            int wall = (int)(GetTickCount() - audio_ticks), position = AudioSamples();
            if (!first_frame_logged) {
                first_frame_logged = 1;
                text[0] = 0;
                AppendNumber(text, "since_run_ms", (int)(GetTickCount() - run_ticks));
                Report("first frame presented:", text);
            }
            if (position > 0 && !probe_started) {
                probe_started = 1;
                text[0] = 0;
                end = AppendNumber(text, "since_audio_start_ms", (int)(GetTickCount() - audio_ticks));
                AppendNumber(end, "since_run_ms", (int)(GetTickCount() - run_ticks));
                Report("sound device began playing:", text);
            }
            // Every five seconds of live running: the wall clock since the sound
            // was written, the sound's position and the picture's sim time, all
            // in milliseconds. sim_minus_audio is how far the picture lags.
            if (!shot_frames && !live_seconds && !record_fps && wall >= next_log_ms) {
                int sim_ms = (int)(sim_time * 1000.f), audio_ms = (int)((float)position * (1000.f / AUDIO_SAMPLE_RATE));
                next_log_ms += 5000;
                text[0] = 0;
                end = AppendNumber(text, "wall", wall);
                end = AppendNumber(end, "sim", sim_ms);
                end = AppendNumber(end, "audio", audio_ms);
                AppendNumber(end, "sim_minus_audio", sim_ms - audio_ms);
                Report("clock:", text);
            }
        }
#endif
    }
    // No teardown: ExitProcess drops the render context, the device context and
    // the sound along with the process.
#ifdef DEBUG
    return 0;
}

void __cdecl WinMainCRTStartup(void) { ExitProcess((UINT)Run()); }
#else
    ExitProcess(0);
}
#endif
