#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

typedef char GLchar;
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* source, const GLint* length);
typedef void (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30

int _fltused = 0;

static PFNGLCREATESHADERPROC glCreateShader_;
static PFNGLSHADERSOURCEPROC glShaderSource_;
static PFNGLCOMPILESHADERPROC glCompileShader_;
static PFNGLCREATEPROGRAMPROC glCreateProgram_;
static PFNGLATTACHSHADERPROC glAttachShader_;
static PFNGLLINKPROGRAMPROC glLinkProgram_;
static PFNGLUSEPROGRAMPROC glUseProgram_;
typedef GLint (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void (APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat value);
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_;
static PFNGLUNIFORM1FPROC glUniform1f_;

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, w_param, l_param);
}

static int LoadOpenGl21(void) {
    glCreateShader_ = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    glShaderSource_ = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    glCompileShader_ = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    glCreateProgram_ = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    glAttachShader_ = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    glLinkProgram_ = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    glUseProgram_ = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
    glGetUniformLocation_ = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
    glUniform1f_ = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
    return glCreateShader_ && glShaderSource_ && glCompileShader_ && glCreateProgram_ && glAttachShader_ && glLinkProgram_ && glUseProgram_ && glGetUniformLocation_ && glUniform1f_;
}

static GLuint BuildProgram(void) {
    const char* vertex_source = "uniform float t;varying vec4 c;void main(){float o=cos(t),s=sin(t);gl_Position=vec4(o*gl_Vertex.x-s*gl_Vertex.y,s*gl_Vertex.x+o*gl_Vertex.y,0.,1.);c=gl_Color;}";
    const char* fragment_source = "varying vec4 c;void main(){gl_FragColor=c;}";
    GLuint vertex = glCreateShader_(GL_VERTEX_SHADER);
    GLuint fragment = glCreateShader_(GL_FRAGMENT_SHADER);
    GLuint program = glCreateProgram_();
    glShaderSource_(vertex, 1, &vertex_source, 0); glCompileShader_(vertex);
    glShaderSource_(fragment, 1, &fragment_source, 0); glCompileShader_(fragment);
    glAttachShader_(program, vertex); glAttachShader_(program, fragment); glLinkProgram_(program);
    return program;
}

static int Run(void) {
    WNDCLASSA window_class = {0}; PIXELFORMATDESCRIPTOR pixel_format = {0}; MSG message;
    HDC device_context; HGLRC render_context; HWND window; int pixel_format_index; GLuint program; GLint time_location;
    window_class.style = CS_OWNDC; window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = GetModuleHandleA(0); window_class.lpszClassName = "in4k.rectangle";
    if (!RegisterClassA(&window_class)) return 1;
#ifdef COMPO
    window = CreateWindowExA(0, window_class.lpszClassName, "", WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 0, 0, window_class.hInstance, 0);
#else
    window = CreateWindowExA(0, window_class.lpszClassName, "in4k rectangle", WS_OVERLAPPEDWINDOW | WS_VISIBLE, (GetSystemMetrics(SM_CXSCREEN) - 1280) / 2, (GetSystemMetrics(SM_CYSCREEN) - 720) / 2, 1280, 720, 0, 0, window_class.hInstance, 0);
#endif
    if (!window) return 2;
    device_context = GetDC(window); pixel_format.nSize = sizeof(pixel_format); pixel_format.nVersion = 1;
    pixel_format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pixel_format.iPixelType = PFD_TYPE_RGBA; pixel_format.cColorBits = 32; pixel_format.cDepthBits = 24; pixel_format.iLayerType = PFD_MAIN_PLANE;
    pixel_format_index = ChoosePixelFormat(device_context, &pixel_format);
    if (!pixel_format_index || !SetPixelFormat(device_context, pixel_format_index, &pixel_format)) return 3;
    render_context = wglCreateContext(device_context);
    if (!render_context || !wglMakeCurrent(device_context, render_context) || !LoadOpenGl21()) return 4;
    program = BuildProgram();
    time_location = glGetUniformLocation_(program, "t");
    for (;;) {
        while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) { if (message.message == WM_QUIT) goto done; TranslateMessage(&message); DispatchMessageA(&message); }
        if (GetAsyncKeyState(VK_ESCAPE)) break;
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glUseProgram_(program);
        glUniform1f_(time_location, (float)GetTickCount() * .001f);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0, 0); glVertex2f(0, .6f);
        glColor3f(0, 1, 0); glVertex2f(-.52f, -.4f);
        glColor3f(0, 0, 1); glVertex2f(.52f, -.4f);
        glEnd(); SwapBuffers(device_context);
    }
done:
    wglMakeCurrent(0, 0); wglDeleteContext(render_context); ReleaseDC(window, device_context); return 0;
}

void __cdecl WinMainCRTStartup(void) { ExitProcess((UINT)Run()); }
