#pragma once

#include "types.hpp"

struct GameInterface {
	void (*init)();
	void (*update)(f32 dt);
	void (*shutdown)();
};

namespace platform {

	enum Key : u8 {
		KEY_NONE = 0,

		KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
		KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
		KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,

		KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,

		KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
		KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,

		KEY_ESCAPE, KEY_TAB, KEY_SPACE, KEY_ENTER, KEY_BACKSPACE,
		KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT, KEY_LEFT_CTRL, KEY_RIGHT_CTRL,
		KEY_LEFT_ALT, KEY_RIGHT_ALT,

		KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,

		KEY_INSERT, KEY_DELETE, KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN,

		KEY_MINUS, KEY_EQUALS, KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET,
		KEY_BACKSLASH, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_COMMA, KEY_PERIOD, KEY_SLASH,
		KEY_GRAVE,

		KEY_COUNT
	};

	enum MouseButton : u8 {
		MOUSE_LEFT = 0,
		MOUSE_RIGHT = 1,
		MOUSE_MIDDLE = 2,
		MOUSE_BUTTON_COUNT = 3
	};

	bool init(const char* title);
	void shutdown();
	void pump_events();
	bool should_quit();

	i32 window_width();
	i32 window_height();

	bool key_down(Key key);
	bool key_pressed(Key key);
	bool key_released(Key key);

	i32 mouse_x();
	i32 mouse_y();
	bool mouse_down(MouseButton button);
	bool mouse_pressed(MouseButton button);
	bool mouse_released(MouseButton button);
	i32 mouse_scroll();

	void set_mouse_pos(i32 x, i32 y);     // client-space coords
	void set_cursor_visible(bool visible);

	bool char_available();
	u32  char_dequeue();
	void char_clear();

	f64 time_seconds();
	f64 delta_time();

	// native handles for Vulkan surface creation
	void* native_window();
	void* native_instance();

	// Threading
	typedef void* Thread;
	typedef void* Event;
	typedef u32 (*ThreadProc)(void* param);

	static constexpr u32 WAIT_INFINITE = 0xFFFFFFFF;

	Thread  create_thread(ThreadProc fn, void* param);
	void    close_thread(Thread t);
	bool    wait_thread(Thread t, u32 timeout_ms);

	Event   create_event();
	void    signal_event(Event e);
	void    close_event(Event e);
	bool    wait_event(Event e, u32 timeout_ms);

}
