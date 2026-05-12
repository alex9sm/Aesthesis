#include "platform.hpp"
#include "log.hpp"
#include "api.hpp"

static void game_init() {
	if (!renderer::init()) {
		logger::fatal("Failed to initialize renderer");
		return;
	}
	logger::info("Game initialized");
}

static void game_update(f32 dt) {

}

static void game_shutdown() {
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
