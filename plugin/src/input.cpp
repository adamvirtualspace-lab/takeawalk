#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>

#include "input.h"
#include "log.h"

namespace input {
namespace {

const wchar_t *const LISTENER_CLASS = L"takeawalk_mouse_listener";

const USHORT USAGE_PAGE_GENERIC = 0x01;
const USHORT USAGE_MOUSE = 0x02;

HANDLE listener_thread = NULL;
HANDLE listener_ready = NULL;
DWORD  listener_thread_id = 0;
HWND   listener_window = NULL;

std::atomic<bool> capturing(false);
std::atomic<long> mouse_x(0);
std::atomic<long> mouse_y(0);

HWND    game_window = NULL;
WNDPROC game_window_procedure = NULL;

// The game's own registration for raw mouse input, put back when capturing ends.
RAWINPUTDEVICE game_mouse_registration = {};
bool           game_mouse_registered = false;

/**
 * @brief Keys the game still gets while on foot: menus, the console and screenshots.
 *
 * Everything else would drive the truck (W revs the engine, Space releases the parking brake).
 */
bool is_game_key(const WPARAM virtual_key)
{
	return
		(virtual_key == VK_ESCAPE) ||
		(virtual_key == VK_OEM_3) ||
		(virtual_key == VK_SNAPSHOT) ||
		(virtual_key == VK_PAUSE) ||
		((virtual_key >= VK_F1) && (virtual_key <= VK_F24))
	;
}

LRESULT CALLBACK game_window_filter(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	const bool key_message = (message == WM_KEYDOWN) || (message == WM_KEYUP);
	if (key_message && capturing && ! is_game_key(wparam)) {
		return 0;
	}
	return CallWindowProcW(game_window_procedure, window, message, wparam, lparam);
}

LRESULT CALLBACK listener_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_INPUT) {
		RAWINPUT data;
		UINT size = sizeof(data);
		const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &data, &size, sizeof(RAWINPUTHEADER));
		const bool relative_mouse = (read != static_cast<UINT>(-1)) && (data.header.dwType == RIM_TYPEMOUSE) && ! (data.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE);
		if (relative_mouse && capturing) {
			mouse_x += data.data.mouse.lLastX;
			mouse_y += data.data.mouse.lLastY;
		}
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

DWORD WINAPI listener_main(LPVOID)
{
	WNDCLASSEXW window_class = {};
	window_class.cbSize = sizeof(window_class);
	window_class.lpfnWndProc = listener_procedure;
	window_class.hInstance = GetModuleHandleW(NULL);
	window_class.lpszClassName = LISTENER_CLASS;
	RegisterClassExW(&window_class);

	listener_window = CreateWindowExW(0, LISTENER_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, window_class.hInstance, NULL);
	SetEvent(listener_ready);

	if (listener_window) {
		MSG message;
		while (GetMessageW(&message, NULL, 0, 0) > 0) {
			DispatchMessageW(&message);
		}
		DestroyWindow(listener_window);
		listener_window = NULL;
	}
	UnregisterClassW(LISTENER_CLASS, window_class.hInstance);
	return 0;
}

void set_mouse_listening(const bool enabled)
{
	// A process has one raw input registration per device type, so listening takes the
	// mouse away from the game. Remember what the game had and give it back afterwards.

	RAWINPUTDEVICE device = {};
	device.usUsagePage = USAGE_PAGE_GENERIC;
	device.usUsage = USAGE_MOUSE;

	if (enabled) {
		RAWINPUTDEVICE registered[16];
		UINT count = 16;
		const UINT found = GetRegisteredRawInputDevices(registered, &count, sizeof(RAWINPUTDEVICE));
		game_mouse_registered = false;
		for (UINT i = 0; (found != static_cast<UINT>(-1)) && (i < found); ++i) {
			const bool mouse = (registered[i].usUsagePage == USAGE_PAGE_GENERIC) && (registered[i].usUsage == USAGE_MOUSE);
			if (mouse && (registered[i].hwndTarget != listener_window)) {
				game_mouse_registration = registered[i];
				game_mouse_registered = true;
			}
		}
		device.dwFlags = RIDEV_INPUTSINK;
		device.hwndTarget = listener_window;
	}
	else if (game_mouse_registered) {
		device = game_mouse_registration;
	}
	else {
		device.dwFlags = RIDEV_REMOVE;
		device.hwndTarget = NULL;
	}

	if (! RegisterRawInputDevices(&device, 1, sizeof(device))) {
		log_message(SCS_LOG_TYPE_warning, "unable to %s mouse listening (error %lu)", enabled ? "start" : "stop", GetLastError());
	}
}

} // namespace

bool init(void)
{
	capturing = false;
	listener_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
	listener_thread = CreateThread(NULL, 0, listener_main, NULL, 0, &listener_thread_id);
	if (! listener_thread) {
		return false;
	}
	WaitForSingleObject(listener_ready, 2000);
	return listener_window != NULL;
}

void shutdown(void)
{
	capture_stop();
	if (listener_thread) {
		PostThreadMessageW(listener_thread_id, WM_QUIT, 0, 0);
		WaitForSingleObject(listener_thread, 2000);
		CloseHandle(listener_thread);
		listener_thread = NULL;
	}
	if (listener_ready) {
		CloseHandle(listener_ready);
		listener_ready = NULL;
	}
}

void capture_start(void)
{
	if (capturing) {
		return;
	}
	mouse_x = 0;
	mouse_y = 0;
	set_mouse_listening(true);

	// The foreground window is the game's: capture only starts from a key press in the game.

	game_window = GetForegroundWindow();
	game_window_procedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(game_window_filter)));
	capturing = true;
}

void capture_stop(void)
{
	if (! capturing) {
		return;
	}
	capturing = false;
	if (game_window_procedure) {
		SetWindowLongPtrW(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(game_window_procedure));
		game_window_procedure = NULL;
	}
	set_mouse_listening(false);
}

void take_mouse_delta(long &x, long &y)
{
	x = mouse_x.exchange(0);
	y = mouse_y.exchange(0);
}

bool key_down(const int virtual_key)
{
	return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

bool game_has_focus(void)
{
	DWORD process_id = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &process_id);
	return process_id == GetCurrentProcessId();
}

} // namespace input
