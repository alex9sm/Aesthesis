#include "platform.hpp"
#include "log.hpp"
#include "api.hpp"
#include "scene.hpp"
#include "camera.hpp"
#include "math.hpp"

static Camera g_camera = {};

static void game_init() {
	if (!renderer::init()) {
		logger::fatal("Failed to initialize renderer");
		return;
	}
	if (!scene::init()) {
		logger::error("Failed to initialize scene");
	}
	camera::init(&g_camera);
	logger::info("Game initialized");
}

static void game_update(f32 dt) {
	camera::update(&g_camera, dt);

	if (platform::key_pressed(platform::KEY_GRAVE)) {
		renderer::cycle_debug_mode();
	}

	f32 aspect = (f32)platform::window_width() / (f32)platform::window_height();
	mat4 view = camera::view(g_camera);
	mat4 projection = camera::projection(g_camera, aspect);

	renderer::begin_frame(view, projection);
	scene::submit(dt);
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
