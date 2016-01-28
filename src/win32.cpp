#pragma warning(push, 3)

#include <windows.h>

#pragma warning(pop)

#include <gl/gl.h>
#include <stdio.h>

//TODO printf does not work with Win32 GUI out of the box. Need to do something with AttachConsole/AllocConsole to make it work.
#define FATAL(message) printf(message); return 1;

static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static int initOpenGl(HDC dc)
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
	int iPfd = ChoosePixelFormat(dc, &pfd);
	if (!iPfd)
	{
		FATAL("Could not find a compatible pixel format");
	}
	if (SetPixelFormat(dc, iPfd, &pfd) == FALSE)
	{
		FATAL("Could not set the pixel format");
	}

	HGLRC rc = wglCreateContext(dc);
	if (!rc)
	{
		FATAL("Could not create the OpenGL render context");
	}
	if (wglMakeCurrent(dc, rc) == FALSE)
	{
		FATAL("Could not set the OpenGL render context");
	}

	return 0;
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
	if (initOpenGl(dc) != 0)
	{
		return 1;
	}

	// cornflower blue
	glClearColor(
		0.3921568627451f, 0.5843137254902f, 0.9294117647059f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	SwapBuffers(dc);

	MSG message = {};
	while (GetMessageA(&message, NULL, 0, 0))
	{
		TranslateMessage(&message);
		DispatchMessageA(&message);
	}

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
