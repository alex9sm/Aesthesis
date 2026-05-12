#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "types.hpp"
#include "platform.hpp"

namespace win32 {

	struct State {
		HWND hwnd;
		HINSTANCE hinstance;

		bool quit;

		bool keys_current[platform::KEY_COUNT];
		bool keys_previous[platform::KEY_COUNT];

		bool mouse_current[platform::MOUSE_BUTTON_COUNT];
		bool mouse_previous[platform::MOUSE_BUTTON_COUNT];

		i32 mouse_x;
		i32 mouse_y;
		i32 scroll_delta;

		u32 char_buffer[32];
		i32 char_head;
		i32 char_tail;
		i32 char_count;

		i64 perf_frequency;
		i64 time_start;
		i64 time_previous;
		f64 dt;
	};

	State* get_state();
	platform::Key translate_key(WPARAM wparam, LPARAM lparam);
	LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

}
