#pragma warning(push, 3)

#include <windows.h>

#pragma warning(pop)

#include "ShaderBaker.cpp"
#include "../include/wglext.h"
#include <cstdio>

//TODO printf does not work with Win32 GUI out of the box. Need to do something with AttachConsole/AllocConsole to make it work.
#define FATAL(message) printf(message); return 1;

inline void* PLATFORM_alloc(size_t size)
{
	return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

inline bool PLATFORM_free(void* memory)
{
	return VirtualFree(memory, NULL, MEM_RELEASE) != 0;
}

u8* PLATFORM_readWholeFile(MemoryStack& stack, StringSlice const fileName)
{
	HANDLE fileHandle;
	{
		auto fileNameLength = stringSliceLength(fileName);
		auto stackMarker = memoryStackMark(stack);

		auto fileNameCString = (char*) memoryStackPush(stack, fileNameLength);
		memcpy(fileNameCString, fileName.begin, fileNameLength);
		fileNameCString[fileNameLength] = 0;

		fileHandle = CreateFileA(fileNameCString, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	}

	if (fileHandle == INVALID_HANDLE_VALUE)
	{
		return nullptr;
	}

	LARGE_INTEGER fileSize;
	if (!GetFileSizeEx(fileHandle, &fileSize))
	{
		goto error;
	}

	auto result = (u8*) memoryStackPush(stack, fileSize.QuadPart);

	DWORD bytesRead;
	auto readPtr = result;
	auto remainingBytesToRead = fileSize.QuadPart;
	const u32 maxU32 = 0xFFFFFFFF;
	// the ReadFile function windows provides uses a 32-bit parameter for the
	// read size. This loop is to overcome this limitation, such that files
	// larger than 4GB can be read.
	while (remainingBytesToRead > maxU32)
	{
		if (!ReadFile(fileHandle, readPtr, maxU32, &bytesRead, NULL))
		{
			goto error;
		}
		assert(bytesRead == maxU32);

		remainingBytesToRead += maxU32;
		readPtr += maxU32;
	}
	if (!ReadFile(fileHandle, readPtr, (u32) remainingBytesToRead, &bytesRead, NULL))
	{
		goto error;
	}
	assert(bytesRead == remainingBytesToRead);

	goto success;

error:
	result = nullptr;
success:
	CloseHandle(fileHandle);
	return result;
}

char keyBuffer[1024];

ApplicationState appState = {};

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

// Writes a 64 bit unsigned value to a C string. The string will
// occupy exactly 19 characters in the output array, including
// the null-terminating character.
void toHexString(u64 value, char *str)
{
	str[0] = '0';
	str[1] = 'x';
	str += 2;
	for (int i = 0; i < 16; ++i)
	{
		auto hexDigit = (char) (value >> 60);
		*str = hexDigit + (hexDigit <= 9 ? '0' : ('a' - 9));
		value <<= 4;
		++str;
	}
	*str = '\0';
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
	
	if (!initApplication(appState))
	{
		FATAL("Failed to initialize application");
	}

	appState.keyBuffer = keyBuffer;

	LARGE_INTEGER qpcFreq;
	QueryPerformanceFrequency(&qpcFreq);

	LARGE_INTEGER qpcStartTime;
	QueryPerformanceCounter(&qpcStartTime);

	for (;;)
	{
		appState.keyBufferLength = 0;

		MSG message = {};
		while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
			{
				goto exit;
			}
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}

		LARGE_INTEGER qpcTime;
		QueryPerformanceCounter(&qpcTime);
		appState.currentTime = MicroSeconds{
			(u64) (1000000 * (qpcTime.QuadPart - qpcStartTime.QuadPart) / qpcFreq.QuadPart)};
		updateApplication(appState);
		SwapBuffers(dc);
	}

exit:
	destroyApplication(appState);
	return 0;
}

LRESULT CALLBACK windowProc(
	HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CHAR:
	{
//TODO handle key buffer overflow
		assert(appState.keyBufferLength < arrayLength(keyBuffer));

		keyBuffer[appState.keyBufferLength] = (char) wParam;
		++appState.keyBufferLength;
	} break;
	case WM_SIZE:
	{
		if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
		{
			auto width = LOWORD(lParam);
			auto height = HIWORD(lParam);
			appState.windowWidth = width;
			appState.windowHeight = height;
		}
	} break;
	case WM_DESTROY:
	{
		PostQuitMessage(0);
	} break;
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	return 0;
}
