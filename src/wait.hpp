#pragma once

typedef void (*WaitingTaskRoutine)();

const wchar_t g_szClassName[] = L"WaitWindowClass";
WaitingTaskRoutine g_waitingTask = NULL;

inline DWORD WINAPI WaitingTaskRoutineThread(LPVOID lpParam) {
	if (g_waitingTask)
		g_waitingTask();

	SendMessage((HWND)lpParam, WM_CLOSE, (WPARAM)0, 0);
	return 0;
}

inline void CreateCtrls(HWND hwnd) {
	RECT rectMain = { 0 };
	GetClientRect(hwnd, &rectMain);

	HWND hwndPB = CreateWindowEx(0, PROGRESS_CLASS, NULL,
		WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
		0, 0, rectMain.right, rectMain.bottom - 5,
		hwnd, (HMENU)0, GetModuleHandle(NULL), NULL);

	SendMessage(hwndPB, PBM_SETMARQUEE, (WPARAM)1, 0);
}

inline LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
		CreateCtrls(hwnd);
		break;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

inline BOOL RegisterWindowClass(HINSTANCE hInstance) {
	HICON icon = LoadIcon(hInstance, L"IDI_MAIN");

	WNDCLASSEX wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = icon;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = g_szClassName;
	wc.hIconSm = icon;
	return RegisterClassEx(&wc);
}

inline HWND CreateMainWindow(HINSTANCE hInstance) {
	int winWidth = 300;
	int winHeight = 60;
	int xPos = (GetSystemMetrics(SM_CXSCREEN) - winWidth) / 2;
	int yPos = (GetSystemMetrics(SM_CYSCREEN) - winHeight) / 2;

	return CreateWindowEx(
		WS_EX_TOPMOST,
		g_szClassName,
		L"Installing, please wait...",
		WS_POPUP | WS_CAPTION,
		xPos, yPos, winWidth, winHeight,
		NULL, NULL, hInstance, NULL);
}

inline void InitCommCtrl() {
	INITCOMMONCONTROLSEX icex = { 0 };
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icex.dwICC = ICC_PROGRESS_CLASS;
	InitCommonControlsEx(&icex);
}

inline int StartWaiting(HINSTANCE hInstance, WaitingTaskRoutine task)
{
	g_waitingTask = task;
	InitCommCtrl();

	if (!RegisterWindowClass(hInstance)) {
		ErrorMsg(L"Window Registration Failed!");
		return 1;
	}

	HWND hwnd = CreateMainWindow(hInstance);
	if (!hwnd) {
		ErrorMsg(L"Window Creation Failed!");
		return 1;
	}

	g_topWindow = hwnd;
	CreateThread(NULL, 0, WaitingTaskRoutineThread, hwnd, 0, NULL);
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	MSG Msg;
	while (GetMessage(&Msg, NULL, 0, 0) > 0) {
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}

	return Msg.wParam;
}
