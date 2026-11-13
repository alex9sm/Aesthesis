#pragma once

#include "types.hpp"

// characters that loop a waypoint path from the level, idling at waypoints that have a `wait`
namespace npc {

	void load();     // spawns the loaded level's `npc` entities. outside begin_frame/end_frame
	void unload();
	void tick(f32 dt);         // one fixed physics step, before physics::step
	void update(f32 dt);       // per frame: advances clips
	void submit(f32 alpha);    // between begin_frame/end_frame. alpha: share of the next physics step elapsed

}
