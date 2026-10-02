#include "scene.hpp"
#include "api.hpp"
#include "log.hpp"
#include "string.hpp"
#include "math.hpp"

namespace scene {

	static renderer::ModelHandle   helmet     = renderer::INVALID_MODEL;
	static renderer::ModelHandle   chess = renderer::INVALID_MODEL;
	static renderer::ModelHandle room = renderer::INVALID_MODEL;
	static renderer::CubemapHandle env_cubemap    = renderer::INVALID_CUBEMAP;
	static renderer::FontHandle    hud_font       = renderer::INVALID_FONT;

	// FPS smoothing — accumulate frames over a one-second window, then publish.
	static f32  fps_accum_time    = 0.0f;
	static u32  fps_accum_frames  = 0;
	static char fps_text[32]      = "FPS: --";

	static f32  light_time        = 0.0f;


	bool init() {
		//helmet = renderer::load_model("assets/models/damagedhelmet/DamagedHelmet.gltf");
		//chess = renderer::load_model("assets/models/chess/chess.gltf");
		room = renderer::load_model("assets/models/testroom/testingroom.glb");

		renderer::set_sun({ 0.38f, 1.0f, 0.41f }, { 1.0f, 1.0f, 1.0f }, 0.0f);
		//env_cubemap = renderer::load_cubemap("field", 1.0f);
		renderer::set_environment_cubemap(env_cubemap);

		hud_font = renderer::load_font("assets/textures/global/NeueHaasDisplayMediu.ttf", 24.0f);
		if (hud_font == renderer::INVALID_FONT) {
			logger::error("Failed to load HUD font");
		}
		return true;
	}

	void shutdown() {
		renderer::unload_font(hud_font);
		renderer::clear_environment_cubemap();
		renderer::unload_cubemap(env_cubemap);
		//renderer::unload_model(helmet);
		//renderer::unload_model(chess);
		renderer::unload_model(room);
	}

	void submit(f32 dt) {

		//renderer::submit_model(helmet, mat4_translate({ 0.0f, 3.0f, 0.0f }));
		//renderer::submit_model(chess);
		renderer::submit_model(room);

		// test point lights
		// renderer::submit_point_light({ 2.0f, 2.0f, 1.0f },  { 1.0f, 0.3f, 0.1f }, 10.0f, 1000.0f);
		// renderer::submit_point_light({-4.0f, 4.0f, 0.0f },  { 0.1f, 0.3f, 1.0f }, 10.0f, 1000.0f);
		// renderer::submit_point_light({ 0.0f, 5.0f, 8.0f },  { 0.2f, 1.0f, 0.2f }, 10.0f, 1000.0f);

		// --- test spot lights ---
		light_time += dt;

		// hard-edged warm spot straight down (inner close to outer), shadowed
		renderer::submit_spot_light({ -2.5f, 7.0f, 2.0f }, { 1.0f, 0.8f, 0.55f }, 14.0f, 1000.0f,
			0.1f, { 0.0f, -1.0f, 0.0f }, 20.0f, 25.0f, true);

		// narrow red spot sweeping a circle across the board (animated lights need nothing special)
		vec3 sweep = { 0.5f * math::sin(light_time * 0.8f), -1.0f, 0.5f * math::cos(light_time * 0.8f) };
		renderer::submit_spot_light({ 0.0f, 8.0f, 0.0f }, { 1.0f, 0.25f, 0.2f }, 16.0f, 1500.0f,
			0.1f, sweep, 8.0f, 12.0f, true);

		// --- FPS HUD ---
		fps_accum_time   += dt;
		fps_accum_frames += 1;
		if (fps_accum_time >= 1.0f) {
			f32 fps = (f32)fps_accum_frames / fps_accum_time;
			str::format(fps_text, sizeof(fps_text), "FPS: %d", (int)(fps + 0.5f));
			fps_accum_time   = 0.0f;
			fps_accum_frames = 0;
		}

		// dark translucent backdrop behind the counter for readability
		renderer::draw_2d_rect(8.0f, 8.0f, 130.0f, 32.0f, { 0.0f, 0.0f, 0.0f, 0.55f });
		renderer::draw_text(hud_font, fps_text, 16.0f, 12.0f, 1.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
	}

}
