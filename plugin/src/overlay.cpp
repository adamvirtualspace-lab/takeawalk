#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>
#include <wchar.h>

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

// The key hint sits in the bottom left corner, above the game's own indicators.
const int HINT_FONT_HEIGHT = 20;	// px
const int HINT_PADDING = 10;		// px, around the lines
const int HINT_LINE_GAP = 8;		// px, between lines
const int KEY_PADDING_X = 9;		// px, inside a key
const int KEY_PADDING_Y = 3;		// px
const int KEY_GAP = 10;			// px, between a key and what it does
const float HINT_LEFT_MARGIN = 0.0125f;
const float HINT_BOTTOM_MARGIN = 0.075f;

const int MAX_HINT_LINES = 4;

const BYTE PANEL_OPACITY = 205;
const COLORREF PANEL_COLOR = RGB(18, 22, 28);
const COLORREF TEXT_COLOR = RGB(255, 255, 255);
const COLORREF KEY_COLOR = RGB(232, 236, 240);
const COLORREF KEY_TEXT_COLOR = RGB(18, 22, 28);

// A faded line: a key which does nothing where the player is now.
const COLORREF FADED_TEXT_COLOR = RGB(120, 126, 133);
const COLORREF FADED_KEY_COLOR = RGB(78, 83, 90);

HANDLE thread = NULL;
HANDLE ready = NULL;
DWORD  thread_id = 0;
HWND   window = NULL;
HWND   hint_window = NULL;
HFONT  font = NULL;
HFONT  hint_font = NULL;

CRITICAL_SECTION lock;
wchar_t          pending_text[256];
HWND             pending_anchor = NULL;
wchar_t          pending_hint[256];
HWND             pending_hint_anchor = NULL;

/**
 * @brief A bitmap with per-pixel transparency to draw a panel into.
 */
struct canvas_t
{
	HDC     screen;
	HDC     memory;
	HBITMAP bitmap;
	HGDIOBJ previous_bitmap;
	DWORD  *pixels;
	int     width;
	int     height;
};

bool canvas_open(canvas_t &canvas, const HDC screen, const HDC memory, const int width, const int height)
{
	BITMAPINFO info = {};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;

	canvas.screen = screen;
	canvas.memory = memory;
	canvas.pixels = NULL;
	canvas.width = width;
	canvas.height = height;
	canvas.bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&canvas.pixels), NULL, 0);
	if (! canvas.bitmap || ! canvas.pixels) {
		return false;
	}
	canvas.previous_bitmap = SelectObject(memory, canvas.bitmap);

	const RECT panel = { 0, 0, width, height };
	const HBRUSH brush = CreateSolidBrush(PANEL_COLOR);
	FillRect(memory, &panel, brush);
	DeleteObject(brush);
	SetBkMode(memory, TRANSPARENT);
	return true;
}

/**
 * @brief Puts the drawn panel on screen in the given window and releases the bitmap.
 */
void canvas_show(canvas_t &canvas, const HWND target, POINT position)
{
	GdiFlush();

	// GDI leaves the alpha channel empty. The whole panel gets the same opacity,
	// and the colours are scaled by it as layered windows expect.

	for (int i = 0; i < canvas.width * canvas.height; ++i) {
		const DWORD color = canvas.pixels[i];
		const DWORD red = ((color >> 16) & 0xFF) * PANEL_OPACITY / 255;
		const DWORD green = ((color >> 8) & 0xFF) * PANEL_OPACITY / 255;
		const DWORD blue = (color & 0xFF) * PANEL_OPACITY / 255;
		canvas.pixels[i] = (static_cast<DWORD>(PANEL_OPACITY) << 24) | (red << 16) | (green << 8) | blue;
	}

	SIZE size = { canvas.width, canvas.height };
	POINT source = { 0, 0 };
	BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	UpdateLayeredWindow(target, canvas.screen, &position, &size, canvas.memory, &source, 0, &blend, ULW_ALPHA);
	ShowWindow(target, SW_SHOWNOACTIVATE);
	SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	SelectObject(canvas.memory, canvas.previous_bitmap);
	DeleteObject(canvas.bitmap);
}

/**
 * @brief Draws the message on a dark panel near the bottom of the anchor window.
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

	canvas_t canvas;
	if (canvas_open(canvas, screen, memory, width, height)) {
		SetTextColor(memory, TEXT_COLOR);
		RECT draw_rect = { PADDING_X, PADDING_Y, width - PADDING_X, height - PADDING_Y };
		DrawTextW(memory, text, -1, &draw_rect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

		const POINT position = {
			anchor_origin.x + (anchor_rect.right - width) / 2,
			anchor_origin.y + static_cast<int>(anchor_rect.bottom * (1.0f - BOTTOM_MARGIN)) - height
		};
		canvas_show(canvas, window, position);
	}

	SelectObject(memory, previous_font);
	DeleteDC(memory);
	ReleaseDC(NULL, screen);
}

/**
 * @brief One line of the key hint: the key, then what it does.
 */
struct hint_line_t
{
	const wchar_t *key;
	int            key_length;
	const wchar_t *action;
	int            action_length;
	SIZE           key_size;
	SIZE           action_size;
	bool           faded;
};

/**
 * @brief Splits "[key] action" lines separated by line feeds. A line starting with '~' is faded.
 */
int split_hint(const wchar_t *text, hint_line_t *const lines)
{
	int count = 0;
	while (*text && (count < MAX_HINT_LINES)) {
		const wchar_t *end = wcschr(text, L'\n');
		if (! end) {
			end = text + wcslen(text);
		}
		hint_line_t &line = lines[count++];
		line = hint_line_t();
		if (*text == L'~') {
			line.faded = true;
			++text;
		}
		const wchar_t *action = text;
		const wchar_t *const bracket = (*text == L'[') ? wmemchr(text, L']', end - text) : NULL;
		if (bracket) {
			line.key = text + 1;
			line.key_length = static_cast<int>(bracket - text - 1);
			action = bracket + 1;
			while ((action < end) && (*action == L' ')) {
				++action;
			}
		}
		line.action = action;
		line.action_length = static_cast<int>(end - action);
		text = *end ? end + 1 : end;
	}
	return count;
}

/**
 * @brief Draws the key hint in the bottom left corner of the anchor window.
 */
void present_hint(const wchar_t *const text, const HWND anchor)
{
	RECT anchor_rect;
	if (! anchor || ! GetClientRect(anchor, &anchor_rect)) {
		return;
	}
	POINT anchor_origin = { 0, 0 };
	ClientToScreen(anchor, &anchor_origin);

	hint_line_t lines[MAX_HINT_LINES];
	const int count = split_hint(text, lines);
	if (count == 0) {
		ShowWindow(hint_window, SW_HIDE);
		return;
	}

	const HDC screen = GetDC(NULL);
	const HDC memory = CreateCompatibleDC(screen);
	const HGDIOBJ previous_font = SelectObject(memory, hint_font);

	// Keys get the same width, so that what they do lines up.

	int key_width = 0;
	int text_height = 0;
	int action_width = 0;
	for (int i = 0; i < count; ++i) {
		hint_line_t &line = lines[i];
		if (line.key) {
			GetTextExtentPoint32W(memory, line.key, line.key_length, &line.key_size);
		}
		GetTextExtentPoint32W(memory, line.action, line.action_length, &line.action_size);
		key_width = max(key_width, static_cast<int>(line.key_size.cx));
		action_width = max(action_width, static_cast<int>(line.action_size.cx));
		text_height = max(text_height, static_cast<int>(max(line.key_size.cy, line.action_size.cy)));
	}
	const int key_box_width = key_width ? key_width + KEY_PADDING_X * 2 : 0;
	const int line_height = text_height + KEY_PADDING_Y * 2;
	const int width = HINT_PADDING * 2 + key_box_width + (key_box_width ? KEY_GAP : 0) + action_width;
	const int height = HINT_PADDING * 2 + count * line_height + (count - 1) * HINT_LINE_GAP;

	canvas_t canvas;
	if (canvas_open(canvas, screen, memory, width, height)) {
		const HBRUSH key_brush = CreateSolidBrush(KEY_COLOR);
		const HBRUSH faded_key_brush = CreateSolidBrush(FADED_KEY_COLOR);
		const HGDIOBJ previous_brush = SelectObject(memory, key_brush);
		const HGDIOBJ previous_pen = SelectObject(memory, GetStockObject(NULL_PEN));
		for (int i = 0; i < count; ++i) {
			const hint_line_t &line = lines[i];
			const int top = HINT_PADDING + i * (line_height + HINT_LINE_GAP);
			if (line.key) {
				SelectObject(memory, line.faded ? faded_key_brush : key_brush);
				RoundRect(memory, HINT_PADDING, top, HINT_PADDING + key_box_width + 1, top + line_height + 1, 8, 8);
				SetTextColor(memory, KEY_TEXT_COLOR);
				RECT key_rect = { HINT_PADDING, top, HINT_PADDING + key_box_width, top + line_height };
				DrawTextW(memory, line.key, line.key_length, &key_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
			SetTextColor(memory, line.faded ? FADED_TEXT_COLOR : TEXT_COLOR);
			RECT action_rect = { HINT_PADDING + key_box_width + (key_box_width ? KEY_GAP : 0), top, width - HINT_PADDING, top + line_height };
			DrawTextW(memory, line.action, line.action_length, &action_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		SelectObject(memory, previous_pen);
		SelectObject(memory, previous_brush);
		DeleteObject(key_brush);
		DeleteObject(faded_key_brush);

		const POINT position = {
			anchor_origin.x + static_cast<int>(anchor_rect.right * HINT_LEFT_MARGIN),
			anchor_origin.y + static_cast<int>(anchor_rect.bottom * (1.0f - HINT_BOTTOM_MARGIN)) - height
		};
		canvas_show(canvas, hint_window, position);
	}

	SelectObject(memory, previous_font);
	DeleteDC(memory);
	ReleaseDC(NULL, screen);
}

LRESULT CALLBACK window_procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message) {
		case MESSAGE_SHOW: {
			const bool hint = (handle == hint_window);
			wchar_t text[256];
			HWND anchor = NULL;
			EnterCriticalSection(&lock);
			memcpy(text, hint ? pending_hint : pending_text, sizeof(text));
			anchor = hint ? pending_hint_anchor : pending_anchor;
			LeaveCriticalSection(&lock);

			if (hint) {
				// Stays until it is taken away.

				present_hint(text, anchor);
			}
			else {
				present(text, anchor);
				SetTimer(handle, HIDE_TIMER, DISPLAY_TIME_MS, NULL);
			}
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

	// Never take focus or mouse clicks, and stay out of the task bar.

	const DWORD style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
	window = CreateWindowExW(style, WINDOW_CLASS, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, window_class.hInstance, NULL);
	hint_window = CreateWindowExW(style, WINDOW_CLASS, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, window_class.hInstance, NULL);
	font = CreateFontW(-FONT_HEIGHT, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	hint_font = CreateFontW(-HINT_FONT_HEIGHT, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	SetEvent(ready);

	if (window && hint_window) {
		MSG message;
		while (GetMessageW(&message, NULL, 0, 0) > 0) {
			DispatchMessageW(&message);
		}
	}
	if (hint_window) {
		DestroyWindow(hint_window);
		hint_window = NULL;
	}
	if (window) {
		DestroyWindow(window);
		window = NULL;
	}
	if (font) {
		DeleteObject(font);
		font = NULL;
	}
	if (hint_font) {
		DeleteObject(hint_font);
		hint_font = NULL;
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
	return (window != NULL) && (hint_window != NULL);
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

void set_hint(const char *const text)
{
	if (! hint_window) {
		return;
	}
	if (! text || ! *text) {
		PostMessageW(hint_window, MESSAGE_HIDE, 0, 0);
		return;
	}
	EnterCriticalSection(&lock);
	MultiByteToWideChar(CP_UTF8, 0, text, -1, pending_hint, 256);
	pending_hint[255] = 0;
	pending_hint_anchor = GetForegroundWindow();
	LeaveCriticalSection(&lock);
	PostMessageW(hint_window, MESSAGE_SHOW, 0, 0);
}

} // namespace overlay
