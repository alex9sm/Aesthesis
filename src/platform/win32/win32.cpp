#include "win32.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "Resource.h"

// NOTE: game provides create_game() which returns a GameInterface
extern GameInterface create_game();

namespace win32 {

	namespace {
		State state = {};
	}

	State* get_state() {
		return &state;
	}

	platform::Key translate_key(WPARAM wparam, LPARAM lparam) {
		switch (wparam) {
			case 'A': return platform::KEY_A;
			case 'B': return platform::KEY_B;
			case 'C': return platform::KEY_C;
			case 'D': return platform::KEY_D;
			case 'E': return platform::KEY_E;
			case 'F': return platform::KEY_F;
			case 'G': return platform::KEY_G;
			case 'H': return platform::KEY_H;
			case 'I': return platform::KEY_I;
			case 'J': return platform::KEY_J;
			case 'K': return platform::KEY_K;
			case 'L': return platform::KEY_L;
			case 'M': return platform::KEY_M;
			case 'N': return platform::KEY_N;
			case 'O': return platform::KEY_O;
			case 'P': return platform::KEY_P;
			case 'Q': return platform::KEY_Q;
			case 'R': return platform::KEY_R;
			case 'S': return platform::KEY_S;
			case 'T': return platform::KEY_T;
			case 'U': return platform::KEY_U;
			case 'V': return platform::KEY_V;
			case 'W': return platform::KEY_W;
			case 'X': return platform::KEY_X;
			case 'Y': return platform::KEY_Y;
			case 'Z': return platform::KEY_Z;

			case '0': return platform::KEY_0;
			case '1': return platform::KEY_1;
			case '2': return platform::KEY_2;
			case '3': return platform::KEY_3;
			case '4': return platform::KEY_4;
			case '5': return platform::KEY_5;
			case '6': return platform::KEY_6;
			case '7': return platform::KEY_7;
			case '8': return platform::KEY_8;
			case '9': return platform::KEY_9;

			case VK_F1:  return platform::KEY_F1;
			case VK_F2:  return platform::KEY_F2;
			case VK_F3:  return platform::KEY_F3;
			case VK_F4:  return platform::KEY_F4;
			case VK_F5:  return platform::KEY_F5;
			case VK_F6:  return platform::KEY_F6;
			case VK_F7:  return platform::KEY_F7;
			case VK_F8:  return platform::KEY_F8;
			case VK_F9:  return platform::KEY_F9;
			case VK_F10: return platform::KEY_F10;
			case VK_F11: return platform::KEY_F11;
			case VK_F12: return platform::KEY_F12;

			case VK_ESCAPE:  return platform::KEY_ESCAPE;
			case VK_TAB:     return platform::KEY_TAB;
			case VK_SPACE:   return platform::KEY_SPACE;
			case VK_RETURN:  return platform::KEY_ENTER;
			case VK_BACK:    return platform::KEY_BACKSPACE;

			case VK_SHIFT: {
				UINT scancode = (lparam >> 16) & 0xFF;
				return MapVirtualKeyA(scancode, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT
					? platform::KEY_RIGHT_SHIFT : platform::KEY_LEFT_SHIFT;
			}
			case VK_CONTROL: {
				return (lparam & (1 << 24)) ? platform::KEY_RIGHT_CTRL : platform::KEY_LEFT_CTRL;
			}
			case VK_MENU: {
				return (lparam & (1 << 24)) ? platform::KEY_RIGHT_ALT : platform::KEY_LEFT_ALT;
			}

			case VK_LEFT:  return platform::KEY_LEFT;
			case VK_RIGHT: return platform::KEY_RIGHT;
			case VK_UP:    return platform::KEY_UP;
			case VK_DOWN:  return platform::KEY_DOWN;

			case VK_INSERT:  return platform::KEY_INSERT;
			case VK_DELETE:  return platform::KEY_DELETE;
			case VK_HOME:    return platform::KEY_HOME;
			case VK_END:     return platform::KEY_END;
			case VK_PRIOR:   return platform::KEY_PAGE_UP;
			case VK_NEXT:    return platform::KEY_PAGE_DOWN;

			case VK_OEM_MINUS:   return platform::KEY_MINUS;
			case VK_OEM_PLUS:    return platform::KEY_EQUALS;
			case VK_OEM_4:       return platform::KEY_LEFT_BRACKET;
			case VK_OEM_6:       return platform::KEY_RIGHT_BRACKET;
			case VK_OEM_5:       return platform::KEY_BACKSLASH;
			case VK_OEM_1:       return platform::KEY_SEMICOLON;
			case VK_OEM_7:       return platform::KEY_APOSTROPHE;
			case VK_OEM_COMMA:   return platform::KEY_COMMA;
			case VK_OEM_PERIOD:  return platform::KEY_PERIOD;
			case VK_OEM_2:       return platform::KEY_SLASH;
			case VK_OEM_3:       return platform::KEY_GRAVE;

			default: return platform::KEY_NONE;
		}
	}

	LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
		switch (msg) {
			case WM_CLOSE: {
				state.quit = true;
				return 0;
			}
			case WM_DESTROY: {
				PostQuitMessage(0);
				return 0;
			}
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN: {
				platform::Key key = translate_key(wparam, lparam);
				if (key != platform::KEY_NONE) {
					state.keys_current[key] = true;
				}
				return 0;
			}
			case WM_KEYUP:
			case WM_SYSKEYUP: {
				platform::Key key = translate_key(wparam, lparam);
				if (key != platform::KEY_NONE) {
					state.keys_current[key] = false;
				}
				return 0;
			}
			case WM_LBUTTONDOWN: {
				state.mouse_current[platform::MOUSE_LEFT] = true;
				return 0;
			}
			case WM_LBUTTONUP: {
				state.mouse_current[platform::MOUSE_LEFT] = false;
				return 0;
			}
			case WM_RBUTTONDOWN: {
				state.mouse_current[platform::MOUSE_RIGHT] = true;
				return 0;
			}
			case WM_RBUTTONUP: {
				state.mouse_current[platform::MOUSE_RIGHT] = false;
				return 0;
			}
			case WM_MBUTTONDOWN: {
				state.mouse_current[platform::MOUSE_MIDDLE] = true;
				return 0;
			}
			case WM_MBUTTONUP: {
				state.mouse_current[platform::MOUSE_MIDDLE] = false;
				return 0;
			}
			case WM_MOUSEMOVE: {
				state.mouse_x = (i32)(short)LOWORD(lparam);
				state.mouse_y = (i32)(short)HIWORD(lparam);
				return 0;
			}
			case WM_MOUSEWHEEL: {
				state.scroll_delta += GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
				return 0;
			}
			case WM_CHAR: {
				u32 codepoint = (u32)wparam;
				if (codepoint >= 32 && state.char_count < 32) {
					state.char_buffer[state.char_tail] = codepoint;
					state.char_tail = (state.char_tail + 1) & 31;
					state.char_count++;
				}
				return 0;
			}
			default:
				return DefWindowProcA(hwnd, msg, wparam, lparam);
		}
	}

}

namespace platform {

	bool init(const char* title) {
		win32::State* s = win32::get_state();

		s->hinstance = GetModuleHandleA(nullptr);

		WNDCLASSEXA wc = {};
		wc.cbSize = sizeof(WNDCLASSEXA);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = win32::wnd_proc;
		wc.hInstance = s->hinstance;
		wc.hIcon = LoadIconA(s->hinstance, MAKEINTRESOURCEA(IDI_AESTHESIS));
		wc.hIconSm = LoadIconA(s->hinstance, MAKEINTRESOURCEA(IDI_SMALL));
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wc.lpszClassName = "AesthesisWindowClass";

		if (!RegisterClassExA(&wc)) {
			logger::fatal("Failed to register window class");
			return false;
		}

		DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
		RECT rc = { 0, 0, 1600, 900 };
		AdjustWindowRectEx(&rc, style, FALSE, 0);
		i32 win_w = rc.right - rc.left;
		i32 win_h = rc.bottom - rc.top;
		i32 screen_w = GetSystemMetrics(SM_CXSCREEN);
		i32 screen_h = GetSystemMetrics(SM_CYSCREEN);

		s->hwnd = CreateWindowExA(
			0,
			"AesthesisWindowClass",
			title,
			style,
			(screen_w - win_w) / 2, (screen_h - win_h) / 2,
			win_w, win_h,
			nullptr, nullptr,
			s->hinstance,
			nullptr
		);

		if (!s->hwnd) {
			logger::fatal("Failed to create window");
			return false;
		}

		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		s->perf_frequency = freq.QuadPart;

		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		s->time_start = now.QuadPart;
		s->time_previous = now.QuadPart;

		s->cursor_visible = true;

		ShowWindow(s->hwnd, SW_SHOW);
		UpdateWindow(s->hwnd);

		logger::info("Platform initialized");
		return true;
	}

	void shutdown() {
		win32::State* s = win32::get_state();
		if (s->hwnd) {
			DestroyWindow(s->hwnd);
			s->hwnd = nullptr;
		}
		UnregisterClassA("AesthesisWindowClass", s->hinstance);
		logger::info("Platform shutdown");
	}

	void pump_events() {
		win32::State* s = win32::get_state();

		memory::copy(s->keys_previous, s->keys_current, sizeof(s->keys_current));
		memory::copy(s->mouse_previous, s->mouse_current, sizeof(s->mouse_current));
		s->scroll_delta = 0;

		MSG msg;
		while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				s->quit = true;
			}
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}

		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		s->dt = (f64)(now.QuadPart - s->time_previous) / (f64)s->perf_frequency;
		s->time_previous = now.QuadPart;
	}

	bool should_quit() {
		return win32::get_state()->quit;
	}

	i32 window_width() {
		RECT r;
		GetClientRect(win32::get_state()->hwnd, &r);
		return r.right - r.left;
	}

	i32 window_height() {
		RECT r;
		GetClientRect(win32::get_state()->hwnd, &r);
		return r.bottom - r.top;
	}

	bool key_down(Key key) {
		return win32::get_state()->keys_current[key];
	}

	bool key_pressed(Key key) {
		win32::State* s = win32::get_state();
		return s->keys_current[key] && !s->keys_previous[key];
	}

	bool key_released(Key key) {
		win32::State* s = win32::get_state();
		return !s->keys_current[key] && s->keys_previous[key];
	}

	i32 mouse_x() { return win32::get_state()->mouse_x; }
	i32 mouse_y() { return win32::get_state()->mouse_y; }

	bool mouse_down(MouseButton button) {
		return win32::get_state()->mouse_current[button];
	}

	bool mouse_pressed(MouseButton button) {
		win32::State* s = win32::get_state();
		return s->mouse_current[button] && !s->mouse_previous[button];
	}

	bool mouse_released(MouseButton button) {
		win32::State* s = win32::get_state();
		return !s->mouse_current[button] && s->mouse_previous[button];
	}

	i32 mouse_scroll() {
		return win32::get_state()->scroll_delta;
	}

	void set_mouse_pos(i32 x, i32 y) {
		win32::State* s = win32::get_state();
		POINT p = { x, y };
		ClientToScreen(s->hwnd, &p);
		SetCursorPos(p.x, p.y);
		s->mouse_x = x;
		s->mouse_y = y;
	}

	void set_cursor_visible(bool visible) {
		win32::State* s = win32::get_state();
		if (s->cursor_visible == visible) return;
		s->cursor_visible = visible;
		// ShowCursor maintains an internal counter; cross 0/1 only once per state change.
		ShowCursor(visible ? TRUE : FALSE);
	}

	bool char_available() {
		return win32::get_state()->char_count > 0;
	}

	u32 char_dequeue() {
		win32::State* s = win32::get_state();
		if (s->char_count <= 0) return 0;
		u32 c = s->char_buffer[s->char_head];
		s->char_head = (s->char_head + 1) & 31;
		s->char_count--;
		return c;
	}

	void char_clear() {
		win32::State* s = win32::get_state();
		s->char_head = 0;
		s->char_tail = 0;
		s->char_count = 0;
	}

	f64 time_seconds() {
		win32::State* s = win32::get_state();
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return (f64)(now.QuadPart - s->time_start) / (f64)s->perf_frequency;
	}

	f64 delta_time() {
		return win32::get_state()->dt;
	}

	void* native_window() {
		return (void*)win32::get_state()->hwnd;
	}

	void* native_instance() {
		return (void*)win32::get_state()->hinstance;
	}

	// --- Threading ---

	struct ThreadTrampoline {
		ThreadProc fn;
		void* param;
	};

	static DWORD WINAPI thread_trampoline(LPVOID lparam) {
		ThreadTrampoline* t = (ThreadTrampoline*)lparam;
		ThreadProc fn = t->fn;
		void* param   = t->param;
		memory::free(t);
		return (DWORD)fn(param);
	}

	Thread create_thread(ThreadProc fn, void* param) {
		ThreadTrampoline* t = (ThreadTrampoline*)memory::malloc(sizeof(ThreadTrampoline));
		t->fn    = fn;
		t->param = param;
		HANDLE h = CreateThread(nullptr, 0, thread_trampoline, t, 0, nullptr);
		if (!h) memory::free(t);
		return (Thread)h;
	}

	void close_thread(Thread t) {
		if (t) CloseHandle((HANDLE)t);
	}

	bool wait_thread(Thread t, u32 timeout_ms) {
		return WaitForSingleObject((HANDLE)t, (DWORD)timeout_ms) == WAIT_OBJECT_0;
	}

	Event create_event() {
		return (Event)CreateEventA(nullptr, FALSE, FALSE, nullptr);
	}

	void signal_event(Event e) {
		SetEvent((HANDLE)e);
	}

	void close_event(Event e) {
		if (e) CloseHandle((HANDLE)e);
	}

	bool wait_event(Event e, u32 timeout_ms) {
		return WaitForSingleObject((HANDLE)e, (DWORD)timeout_ms) == WAIT_OBJECT_0;
	}

}

int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE prev, LPSTR cmd_line, int show_cmd) {

	if (!platform::init("Aesthesis")) {
		return 1;
	}

	GameInterface game = create_game();
	game.init();

	while (!platform::should_quit()) {
		platform::pump_events();
		f32 dt = (f32)platform::delta_time();
		game.update(dt);
	}

	game.shutdown();
	platform::shutdown();

	return 0;
}
