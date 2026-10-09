#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>

#include "overlay.h"

namespace overlay {
namespace {

const wchar_t *const WINDOW_CLASS = L"takeawalk_overlay";

const UINT MESSAGE_SHOW = WM_APP + 1;
const UINT MESSAGE_HIDE = WM_APP + 2;
const UINT_PTR HIDE_TIMER = 1;

const UINT DISPLAY_TIME_MS = 4000;

const int FONT_HEIGHT = 26;		// px
const int PADDING_X = 24;		// px
const int PADDING_Y = 12;		// px
const int MAX_TEXT_WIDTH = 1100;	// px

// Distance of the message from the bottom of the game window, as a fraction of its height.
const float BOTTOM_MARGIN = 0.16f;

const BYTE PANEL_OPACITY = 205;
const COLORREF PANEL_COLOR = RGB(18, 22, 28);
const COLORREF TEXT_COLOR = RGB(255, 255, 255);

HANDLE thread = NULL;
HANDLE ready = NULL;
DWORD  thread_id = 0;
HWND   window = NULL;
HFONT  font = NULL;

CRITICAL_SECTION lock;
wchar_t          pending_text[256];
HWND             pending_anchor = NULL;

/**
 * @brief Draws the text on a dark panel and puts the result on screen.
 *
 * The window has per-pixel transparency, so it is drawn into a bitmap first.
 */
void present(const wchar_t *const text, const HWND anchor)
{
	RECT anchor_rect;
	if (! anchor || ! GetClientRect(anchor, &anchor_rect)) {
		return;
	}
	POINT anchor_origin = { 0, 0 };
	ClientToScreen(anchor, &anchor_origin);

	const HDC screen = GetDC(NULL);
	const HDC memory = CreateCompatibleDC(screen);
	const HGDIOBJ previous_font = SelectObject(memory, font);

	RECT text_rect = { 0, 0, MAX_TEXT_WIDTH, 0 };
	DrawTextW(memory, text, -1, &text_rect, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
	const int width = text_rect.right + PADDING_X * 2;
	const int height = text_rect.bottom + PADDING_Y * 2;

	BITMAPINFO info = {};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;

	DWORD *pixels = NULL;
	const HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels), NULL, 0);
	if (bitmap && pixels) {
		const HGDIOBJ previous_bitmap = SelectObject(memory, bitmap);

		const RECT panel = { 0, 0, width, height };
		const HBRUSH brush = CreateSolidBrush(PANEL_COLOR);
		FillRect(memory, &panel, brush);
		DeleteObject(brush);

		SetBkMode(memory, TRANSPARENT);
		SetTextColor(memory, TEXT_COLOR);
		RECT draw_rect = { PADDING_X, PADDING_Y, width - PADDING_X, height - PADDING_Y };
		DrawTextW(memory, text, -1, &draw_rect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
		GdiFlush();

		// GDI leaves the alpha channel empty. The whole panel gets the same opacity,
		// and the colours are scaled by it as layered windows expect.

		for (int i = 0; i < width * height; ++i) {
			const DWORD color = pixels[i];
			const DWORD red = ((color >> 16) & 0xFF) * PANEL_OPACITY / 255;
			const DWORD green = ((color >> 8) & 0xFF) * PANEL_OPACITY / 255;
			const DWORD blue = (color & 0xFF) * PANEL_OPACITY / 255;
			pixels[i] = (static_cast<DWORD>(PANEL_OPACITY) << 24) | (red << 16) | (green << 8) | blue;
		}

		POINT position = {
			anchor_origin.x + (anchor_rect.right - width) / 2,
			anchor_origin.y + static_cast<int>(anchor_rect.bottom * (1.0f - BOTTOM_MARGIN)) - height
		};
		SIZE size = { width, height };
		POINT source = { 0, 0 };
		BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		UpdateLayeredWindow(window, screen, &position, &size, memory, &source, 0, &blend, ULW_ALPHA);
		ShowWindow(window, SW_SHOWNOACTIVATE);
		SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

		SelectObject(memory, previous_bitmap);
		DeleteObject(bitmap);
	}

	SelectObject(memory, previous_font);
	DeleteDC(memory);
	ReleaseDC(NULL, screen);
}

LRESULT CALLBACK window_procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message) {
		case MESSAGE_SHOW: {
			wchar_t text[256];
			HWND anchor = NULL;
			EnterCriticalSection(&lock);
			memcpy(text, pending_text, sizeof(text));
			anchor = pending_anchor;
			LeaveCriticalSection(&lock);

			present(text, anchor);
			SetTimer(handle, HIDE_TIMER, DISPLAY_TIME_MS, NULL);
			return 0;
		}
		case MESSAGE_HIDE:
		case WM_TIMER: {
			KillTimer(handle, HIDE_TIMER);
			ShowWindow(handle, SW_HIDE);
			return 0;
		}
	}
	return DefWindowProcW(handle, message, wparam, lparam);
}

DWORD WINAPI thread_main(LPVOID)
{
	WNDCLASSEXW window_class = {};
	window_class.cbSize = sizeof(window_class);
	window_class.lpfnWndProc = window_procedure;
	window_class.hInstance = GetModuleHandleW(NULL);
	window_class.lpszClassName = WINDOW_CLASS;
	RegisterClassExW(&window_class);

	// Never takes focus or mouse clicks, and stays out of the task bar.

	const DWORD style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
	window = CreateWindowExW(style, WINDOW_CLASS, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, window_class.hInstance, NULL);
	font = CreateFontW(-FONT_HEIGHT, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	SetEvent(ready);

	if (window) {
		MSG message;
		while (GetMessageW(&message, NULL, 0, 0) > 0) {
			DispatchMessageW(&message);
		}
		DestroyWindow(window);
		window = NULL;
	}
	if (font) {
		DeleteObject(font);
		font = NULL;
	}
	UnregisterClassW(WINDOW_CLASS, window_class.hInstance);
	return 0;
}

} // namespace

bool init(void)
{
	InitializeCriticalSection(&lock);
	ready = CreateEventW(NULL, TRUE, FALSE, NULL);
	thread = CreateThread(NULL, 0, thread_main, NULL, 0, &thread_id);
	if (! thread) {
		return false;
	}
	WaitForSingleObject(ready, 2000);
	return window != NULL;
}

void shutdown(void)
{
	if (thread) {
		PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
		WaitForSingleObject(thread, 2000);
		CloseHandle(thread);
		thread = NULL;
		DeleteCriticalSection(&lock);
	}
	if (ready) {
		CloseHandle(ready);
		ready = NULL;
	}
}

void show(const char *const text)
{
	if (! window) {
		return;
	}

	// Shown over whichever window is in front, which is the game when a key press caused this.

	EnterCriticalSection(&lock);
	MultiByteToWideChar(CP_UTF8, 0, text, -1, pending_text, 256);
	pending_text[255] = 0;
	pending_anchor = GetForegroundWindow();
	LeaveCriticalSection(&lock);
	PostMessageW(window, MESSAGE_SHOW, 0, 0);
}

void hide(void)
{
	if (window) {
		PostMessageW(window, MESSAGE_HIDE, 0, 0);
	}
}

} // namespace overlay
