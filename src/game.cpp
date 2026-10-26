#include "platform.hpp"
#include "log.hpp"
#include "api.hpp"
#include "level.hpp"
#include "camera.hpp"
#include "player.hpp"
#include "viewmodel.hpp"
#include "physics.hpp"
#include "math.hpp"
#include "string.hpp"

static constexpr const char* LEVEL_PATH     = "assets/levels/testingroom/testingroom.gltf";
static constexpr const char* VIEWMODEL      = "weapons/sidearm";

// camera recoil per shot, degrees: pitch up, yaw random within ±. moves to the weapon in V2
static constexpr f32 KICK_PITCH = 1.2f;
static constexpr f32 KICK_YAW   = 0.3f;
static u32 kick_seed = 0x9E3779B9u;

static constexpr f32 WALK_SPEED = 0.5f;   // m/s on the ground before the viewmodel walks

static Camera g_camera = {};
static Player g_player = {};
static bool   g_player_mode = false;   // V toggles freecam <-> player

static constexpr f32 PHYSICS_DT        = 1.0f / 60.0f;
static constexpr u32 MAX_PHYSICS_STEPS = 4;
static f32 physics_accum = 0.0f;

static renderer::FontHandle hud_font = renderer::INVALID_FONT;
static f32  fps_accum_time   = 0.0f;
static u32  fps_accum_frames = 0;
static char fps_text[32]     = "FPS: --";

static void game_init() {
	if (!renderer::init()) {
		logger::fatal("Failed to initialize renderer");
		return;
	}
	if (!physics::init()) {
		logger::fatal("Failed to initialize physics");
		return;
	}
	camera::init(&g_camera);
	player::init(&g_player);

	level::load(LEVEL_PATH);
	viewmodel::load(VIEWMODEL);
	vec3 feet;
	f32  yaw;
	if (level::player_spawn(&feet, &yaw)) {
		player::spawn(&g_player, feet);
		g_camera.yaw  = yaw;
		g_player_mode = true;
		camera::set_captured(&g_camera, true);
	}

	hud_font = renderer::load_font("assets/textures/global/NeueHaasDisplayMediu.ttf", 24.0f);
	if (hud_font == renderer::INVALID_FONT) logger::error("Failed to load HUD font");
	logger::info("Game initialized");
}

static void draw_hud(f32 dt) {
	// publish once per second so the number is readable
	fps_accum_time   += dt;
	fps_accum_frames += 1;
	if (fps_accum_time >= 1.0f) {
		f32 fps = (f32)fps_accum_frames / fps_accum_time;
		str::format(fps_text, sizeof(fps_text), "FPS: %d", (int)(fps + 0.5f));
		fps_accum_time   = 0.0f;
		fps_accum_frames = 0;
	}
	renderer::draw_2d_rect(8.0f, 8.0f, 130.0f, 32.0f, { 0.0f, 0.0f, 0.0f, 0.55f });
	renderer::draw_text(hud_font, fps_text, 16.0f, 12.0f, 1.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
}

static void game_update(f32 dt) {
	// re-read the level, its shared models and the viewmodel from disk; the player and camera stay put
	if (platform::key_pressed(platform::KEY_F5)) {
		viewmodel::unload();
		level::unload();
		level::load(LEVEL_PATH);
		viewmodel::load(VIEWMODEL);
	}

	if (platform::key_pressed(platform::KEY_V)) {
		g_player_mode = !g_player_mode;
		if (g_player_mode) player::spawn(&g_player, g_camera.position - vec3{ 0.0f, player::EYE_HEIGHT, 0.0f });
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

	viewmodel::Input vm = {};
	vm.fire   = platform::mouse_pressed(platform::MOUSE_LEFT);
	vm.reload = platform::key_pressed(platform::KEY_R);
	vm.toggle = platform::key_pressed(platform::KEY_1);
	if (g_player_mode) {
		vec3 v = g_player.velocity;
		vm.walking = physics::character_state(g_player.character).grounded
			&& v.x * v.x + v.z * v.z > WALK_SPEED * WALK_SPEED;
	}
	if (viewmodel::update(dt, vm)) {
		f32 yaw = KICK_YAW * (2.0f * rand_next(&kick_seed) - 1.0f);
		camera::add_kick(&g_camera, to_radians(KICK_PITCH), to_radians(yaw));
	}
	camera::update_kick(&g_camera, dt);

	f32 aspect = (f32)platform::window_width() / (f32)platform::window_height();
	mat4 view = camera::view(g_camera);
	mat4 projection = camera::projection(g_camera, aspect);

	renderer::begin_frame(view, projection);
	level::submit(g_camera.position);
	viewmodel::submit(view, g_camera.fov_y);
	draw_hud(dt);
	renderer::end_frame();
}

static void game_shutdown() {
	viewmodel::unload();
	level::unload();
	renderer::unload_font(hud_font);
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
