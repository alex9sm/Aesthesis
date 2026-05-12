#include "lighting.hpp"
#include "string.hpp"
#include <math.h>

namespace lighting {

	void evaluate(const Setup& setup, f32 time, GpuLightArrays& out, i32& out_count) {
		out_count = 0;

		for (i32 i = 0; i < setup.light_count; i++) {
			const Light& light = setup.lights[i];
			if (!light.enabled) continue;

			f32 mod = 1.0f;
			f32 phase = time * light.behavior_freq + light.behavior_phase;

			switch (light.behavior) {
				case BEHAVIOR_STATIC: break;
				case BEHAVIOR_BLINK:
					mod = sinf(phase * TAU) > 0.0f ? 1.0f : 0.0f;
					break;
				case BEHAVIOR_PULSE:
					mod = 0.5f + 0.5f * sinf(phase * TAU);
					break;
				case BEHAVIOR_FLASH:
					mod = fmaxf(0.0f, sinf(phase * TAU));
					mod = mod * mod;
					break;
				default: break;
			}

			i32 idx = out_count;
			out.positions[idx * 3 + 0] = light.position.x;
			out.positions[idx * 3 + 1] = light.position.y;
			out.positions[idx * 3 + 2] = light.position.z;

			out.colors[idx * 4 + 0] = light.color.x;
			out.colors[idx * 4 + 1] = light.color.y;
			out.colors[idx * 4 + 2] = light.color.z;
			out.colors[idx * 4 + 3] = light.color.w;

			out.radii[idx] = light.radius;
			out.intensities[idx] = light.intensity * mod;

			out_count++;
		}
	}

	const char* behavior_name(Behavior b) {
		switch (b) {
			case BEHAVIOR_STATIC: return "static";
			case BEHAVIOR_BLINK:  return "blink";
			case BEHAVIOR_PULSE:  return "pulse";
			case BEHAVIOR_FLASH:  return "flash";
			default: return "unknown";
		}
	}

	Behavior behavior_from_name(const char* name) {
		if (str::equal(name, "blink"))  return BEHAVIOR_BLINK;
		if (str::equal(name, "pulse"))  return BEHAVIOR_PULSE;
		if (str::equal(name, "flash"))  return BEHAVIOR_FLASH;
		return BEHAVIOR_STATIC;
	}

}
