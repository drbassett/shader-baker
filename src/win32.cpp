#pragma warning(push, 3)

#include <windows.h>

#pragma warning(pop)

#include <stdio.h>

#include <gl/gl.h>
#include "../include/glcorearb.h"
#include "../include/wglext.h"
#include "glFunctions.cpp"

static const char* VS_SOURCE = R"(
#version 330

void main()
{
	gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

static const char* FS_SOURCE = R"(
#version 330

out vec4 color;

void main()
{
	color = vec4(0.8, 0.0, 0.0, 1.0);
}
)";

//TODO printf does not work with Win32 GUI out of the box. Need to do something with AttachConsole/AllocConsole to make it work.
#define FATAL(message) printf(message); return 1;

static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// finds a particular extension in a string containting
// all the extensions, separated by spaces
static bool hasGlExtension(
	const char *allExtensions,
	const char *extensionToFind)
{
	while(*allExtensions != 0)
	{
		// compare the extensionToFind to the next extension
		const char *c = extensionToFind;
		for (;;)
		{
			if (*c == 0)
			{
				if (*allExtensions == ' ' || *allExtensions == 0)
				{
					return true;
				} else
				{
					break;
				}
			} else if (*allExtensions == 0)
			{
				return false;
			} else if (*c != *allExtensions)
			{
				break;
			} else
			{
				++c;
				++allExtensions;
			}
		}

		// advance to the character after the next space
		while (*allExtensions != ' ')
		{
			++allExtensions;
			if (*allExtensions == 0)
			{
				return false;
			}
		}
		++allExtensions;
	}

	return false;
}

static bool initOpenGl(HDC dc)
{
	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags =
		PFD_DRAW_TO_WINDOW |
		PFD_SUPPORT_OPENGL |
		PFD_GENERIC_ACCELERATED |
		PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 24;
	pfd.cRedBits = 8;
	pfd.cGreenBits = 8;
	pfd.cBlueBits = 8;
	int iPfOld = ChoosePixelFormat(dc, &pfd);
	if (!iPfOld)
	{
		return false;
	}
	if (SetPixelFormat(dc, iPfOld, &pfd) == FALSE)
	{
		return false;
	}

	HGLRC oldRc = wglCreateContext(dc);
	if (!oldRc || wglMakeCurrent(dc, oldRc) == FALSE)
	{
		return false;
	}

	PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB =
		(PFNWGLGETEXTENSIONSSTRINGARBPROC) wglGetProcAddress("wglGetExtensionsStringARB");
	if (!wglGetExtensionsStringARB)
	{
		return false;
	}

	const char *extensions = wglGetExtensionsStringARB(dc);
	if (!hasGlExtension(extensions, "WGL_ARB_create_context")
		|| !hasGlExtension(extensions, "WGL_ARB_pixel_format"))
	{
		return false;
	}

	PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB =
		(PFNWGLCHOOSEPIXELFORMATARBPROC) wglGetProcAddress("wglChoosePixelFormatARB");
	PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB =
		(PFNWGLCREATECONTEXTATTRIBSARBPROC) wglGetProcAddress("wglCreateContextAttribsARB");
	if (!wglChoosePixelFormatARB || !wglCreateContextAttribsARB)
	{
		return false;
	}

	int pfAttribsI[] = {
		WGL_DRAW_TO_WINDOW_ARB, 1,
		WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
		WGL_SUPPORT_OPENGL_ARB, 1,
		WGL_DOUBLE_BUFFER_ARB, 1,
		WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
		WGL_COLOR_BITS_ARB, 24,
		WGL_RED_BITS_ARB, 8,
		WGL_GREEN_BITS_ARB, 8,
		WGL_BLUE_BITS_ARB, 8,
		WGL_DEPTH_BITS_ARB, 16,
		0};
	int iPfModern;
	UINT pfCount;
	BOOL choosePfResult = wglChoosePixelFormatARB(
		dc, pfAttribsI, NULL, 1, &iPfModern, &pfCount);
	if (choosePfResult == FALSE || pfCount == 0)
	{
		return false;
	}
	
	pfd = {};
	if (!DescribePixelFormat(dc, iPfModern, sizeof(pfd), &pfd))
	{
		return false;
	}
		
	if (SetPixelFormat(dc, iPfModern, &pfd) == FALSE)
	{
		return false;
	}

	int contextAttribs[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
		WGL_CONTEXT_MINOR_VERSION_ARB, 3,
		WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		0};
	HGLRC modernRc = wglCreateContextAttribsARB(dc, 0, contextAttribs);
	if (!modernRc || wglMakeCurrent(dc, modernRc) == FALSE)
	{
		return false;
	}

	if (wglDeleteContext(oldRc) == FALSE)
	{
//TODO log failure
	}

	if (!initGlFunctions())
	{
		return false;
	}

	return true;
}

bool compileShaderChecked(
	GLuint shader,
	const char* source,
	GLsizei maxLogLength,
	GLchar *log,
	GLsizei& logLength)
{
	glShaderSource(shader, 1, &source, 0);
	glCompileShader(shader);

	GLint compileStatus;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
	if (compileStatus == GL_TRUE)
	{
		return true;
	}

	// GLint infoLogLength;
	// glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);

	glGetShaderInfoLog(
		shader, maxLogLength, &logLength, log);

	return false;
}

bool linkProgramChecked(
	GLuint program,
	GLsizei maxLogLength,
	GLchar *log,
	GLsizei& logLength)
{
	glLinkProgram(program);

	GLint linkStatus;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	if (linkStatus == GL_TRUE)
	{
		return true;
	}

	// GLint infoLogLength;
	// glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);

	glGetProgramInfoLog(
		program, maxLogLength, &logLength, log);

	return false;
}

int CALLBACK WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow)
{
	const char windowClassName[] = "Shader Baker Class";

	WNDCLASS wc = {};
	wc.style = CS_VREDRAW | CS_HREDRAW | CS_OWNDC;
	wc.lpfnWndProc = windowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = windowClassName;

	RegisterClassA(&wc);

	HWND window = CreateWindowExA(
		0,
		windowClassName,
		"Shader Baker",
		WS_MAXIMIZE | WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,
		NULL,
		hInstance,
		NULL);

	if (!window)
	{
		FATAL("Failed to create window");
	}

	HDC dc = GetDC(window);
	if (!initOpenGl(dc))
	{
		FATAL("Failed to initialize OpenGL");
	}

	// cornflower blue
	glClearColor(
		0.3921568627451f, 0.5843137254902f, 0.9294117647059f, 1.0f);

	GLsizei maxLogLength = 1024;
	GLchar infoLog[1024];
	GLsizei logLength;

	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

	if (!compileShaderChecked(vs, VS_SOURCE, maxLogLength, infoLog, logLength))
	{
		FATAL("Failed to compile vertex shader");
	}
	if (!compileShaderChecked(fs, FS_SOURCE, maxLogLength, infoLog, logLength))
	{
		FATAL("Failed to compile fragment shader");
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	if (!linkProgramChecked(program, maxLogLength, infoLog, logLength))
	{
		FATAL("Failed to link program");
	}
	glDetachShader(program, vs);
	glDetachShader(program, fs);

	glDeleteShader(vs);
	glDeleteShader(fs);

	GLuint vao;
	glGenVertexArrays(1, &vao);

	glBindVertexArray(vao);
	glUseProgram(program);

	glClear(GL_COLOR_BUFFER_BIT);
	glPointSize(10.0f);
	glDrawArrays(GL_POINTS, 0, 1);

	SwapBuffers(dc);

	MSG message = {};
	while (GetMessageA(&message, NULL, 0, 0))
	{
		TranslateMessage(&message);
		DispatchMessageA(&message);
	}

	glDeleteProgram(program);
	glDeleteVertexArrays(1, &vao);

	return 0;
}

LRESULT CALLBACK windowProc(
	HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	return 0;
}
