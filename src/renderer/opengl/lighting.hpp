#pragma once

#include "math.hpp"

namespace lighting {

	constexpr i32 MAX_LIGHTS = 16;

	enum Behavior : u8 {
		BEHAVIOR_STATIC = 0,
		BEHAVIOR_BLINK,
		BEHAVIOR_PULSE,
		BEHAVIOR_FLASH,
		BEHAVIOR_COUNT
	};

	struct Light {
		vec3 position;
		vec4 color;
		f32  radius;
		f32  intensity;
		Behavior behavior;
		f32  behavior_freq;
		f32  behavior_phase;
		bool enabled;
	};

	struct Setup {
		Light lights[MAX_LIGHTS];
		i32   light_count;
		vec4  ambient;
	};

	struct GpuLightArrays {
		f32 positions[MAX_LIGHTS * 3];
		f32 colors[MAX_LIGHTS * 4];
		f32 radii[MAX_LIGHTS];
		f32 intensities[MAX_LIGHTS];
	};

	void evaluate(const Setup& setup, f32 time, GpuLightArrays& out, i32& out_count);

	const char* behavior_name(Behavior b);
	Behavior behavior_from_name(const char* name);

}
