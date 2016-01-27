#pragma warning(push, 3)

#include <windows.h>

#pragma warning(pop)

#include <stdio.h>

LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int CALLBACK WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow)
{
	const char windowClassName[] = "Shader Baker Class";

	WNDCLASS wc = {};
	wc.lpfnWndProc = windowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = windowClassName;

	RegisterClassA(&wc);

	HWND window = CreateWindowExA(
		0,
		windowClassName,
		"Shader Baker",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,
		NULL,
		hInstance,
		NULL);

	if (!window)
	{
		printf("Failed to create window");
		return 1;
	}

	ShowWindow(window, nCmdShow);

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
		return 0;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
