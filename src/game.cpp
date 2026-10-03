#include "platform.hpp"
#include "log.hpp"
#include "api.hpp"
#include "scene.hpp"
#include "camera.hpp"
#include "player.hpp"
#include "physics.hpp"
#include "math.hpp"

static Camera g_camera = {};
static Player g_player = {};
static bool   g_player_mode = false;   // V toggles freecam <-> player

static constexpr f32 PHYSICS_DT        = 1.0f / 60.0f;
static constexpr u32 MAX_PHYSICS_STEPS = 4;
static f32 physics_accum = 0.0f;

static void game_init() {
	if (!renderer::init()) {
		logger::fatal("Failed to initialize renderer");
		return;
	}
	if (!physics::init()) {
		logger::fatal("Failed to initialize physics");
		return;
	}
	if (!scene::init()) {
		logger::error("Failed to initialize scene");
	}
	camera::init(&g_camera);
	player::init(&g_player);
	logger::info("Game initialized");
}

static void game_update(f32 dt) {
	if (platform::key_pressed(platform::KEY_V)) {
		g_player_mode = !g_player_mode;
		if (g_player_mode) player::spawn(&g_player, g_camera.position);
		camera::set_captured(&g_camera, g_player_mode);
	}

	if (g_player_mode) {
		camera::look(&g_camera);
		player::input(&g_player);
	} else {
		camera::update(&g_camera, dt);
	}

	physics_accum += dt;
	for (u32 i = 0; i < MAX_PHYSICS_STEPS && physics_accum >= PHYSICS_DT; i++) {
		if (g_player_mode) player::tick(&g_player, g_camera.yaw, PHYSICS_DT);
		physics::step(PHYSICS_DT);
		physics_accum -= PHYSICS_DT;
	}
	// hit the step cap: drop the backlog instead of spiralling
	if (physics_accum >= PHYSICS_DT) physics_accum = 0.0f;

	if (g_player_mode) g_camera.position = player::eye(g_player, physics_accum / PHYSICS_DT);

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
	player::shutdown(&g_player);
	physics::shutdown();
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
