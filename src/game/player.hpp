#pragma once

#include "types.hpp"
#include "math.hpp"
#include "physics.hpp"

struct Player {
	physics::CharacterHandle character;
	vec3 position;        // feet, at the latest physics tick
	vec3 prev_position;   // feet, one tick earlier (for render interpolation)
	vec3 velocity;
	bool jump_queued;     // latched per frame, consumed by the next tick
};

namespace player {

	void init(Player* p);
	void shutdown(Player* p);

	void spawn(Player* p, vec3 eye);
	void input(Player* p);                    // every frame
	void tick(Player* p, f32 yaw, f32 dt);    // every fixed physics step

	// alpha = fraction of the way into the next physics tick
	vec3 eye(const Player& p, f32 alpha);

}
