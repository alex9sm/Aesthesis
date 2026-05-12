#include "platform.hpp"
#include "log.hpp"
#include "api.hpp"
#include "scene.hpp"
#include "math.hpp"

// Vulkan-corrected perspective: maps view-space [-near, -far] to clip-space Z [0, 1].
// pair with a viewport that flips Y so the GL-style winding/UV conventions hold.
static mat4 mat4_perspective_vk(f32 fov_y, f32 aspect, f32 z_near, f32 z_far) {
	f32 f = 1.0f / tanf(fov_y * 0.5f);
	mat4 m = {};
	m.col[0][0] = f / aspect;
	m.col[1][1] = f;
	m.col[2][2] = z_far / (z_near - z_far);
	m.col[2][3] = -1.0f;
	m.col[3][2] = (z_near * z_far) / (z_near - z_far);
	return m;
}

static void game_init() {
	if (!renderer::init()) {
		logger::fatal("Failed to initialize renderer");
		return;
	}
	if (!scene::init()) {
		logger::error("Failed to initialize scene");
	}
	logger::info("Game initialized");
}

static void game_update(f32 dt) {
	f32 aspect = (f32)platform::window_width() / (f32)platform::window_height();
	mat4 view = mat4_look_at({ 0.0f, 1.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
	mat4 projection = mat4_perspective_vk(to_radians(60.0f), aspect, 0.1f, 100.0f);

	renderer::begin_frame(view, projection);
	scene::submit();
	renderer::end_frame();
}

static void game_shutdown() {
	scene::shutdown();
	renderer::shutdown();
	logger::info("Game shutdown");
}

GameInterface create_game() {
	GameInterface game = {};
	game.init = game_init;
	game.update = game_update;
	game.shutdown = game_shutdown;
	return game;
}
