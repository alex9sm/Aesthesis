#include "scene.hpp"
#include "api.hpp"
#include "log.hpp"
#include "string.hpp"

namespace scene {

	static renderer::ModelHandle   helmet     = renderer::INVALID_MODEL;
	static renderer::ModelHandle   chess = renderer::INVALID_MODEL;
	static renderer::CubemapHandle env_cubemap    = renderer::INVALID_CUBEMAP;
	static renderer::FontHandle    hud_font       = renderer::INVALID_FONT;

	// FPS smoothing — accumulate frames over a one-second window, then publish.
	static f32  fps_accum_time    = 0.0f;
	static u32  fps_accum_frames  = 0;
	static char fps_text[32]      = "FPS: --";


	bool init() {
		helmet = renderer::load_model("assets/models/damagedhelmet/DamagedHelmet.gltf");
		chess = renderer::load_model("assets/models/chess/chess.gltf");

		renderer::set_sun({ 0.38f, 1.0f, 0.41f }, { 1.0f, 1.0f, 1.0f }, 0.0f);
		env_cubemap = renderer::load_cubemap("whitestudio", 3.0f);
		if (env_cubemap != renderer::INVALID_CUBEMAP) {
			renderer::set_environment_cubemap(env_cubemap);
		}

		hud_font = renderer::load_font("assets/textures/global/NeueHaasDisplayMediu.ttf", 24.0f);
		if (hud_font == renderer::INVALID_FONT) {
			logger::error("Failed to load HUD font");
		}
		return true;
	}

	void shutdown() {
		if (hud_font != renderer::INVALID_FONT) {
			renderer::unload_font(hud_font);
		}
		if (env_cubemap != renderer::INVALID_CUBEMAP) {
			renderer::clear_environment_cubemap();
			renderer::unload_cubemap(env_cubemap);
		}
		renderer::unload_model(helmet);
		renderer::unload_model(chess);
	}

	void submit(f32 dt) {

		renderer::submit_model(helmet, mat4_translate({ 0.0f, 3.0f, 0.0f }));
		renderer::submit_model(chess);

		// test point lights
		//renderer::submit_light({ 4.0f, 4.0f, 0.0f },  { 1.0f, 0.3f, 0.1f }, 10.0f, 30.0f);
		//renderer::submit_light({-4.0f, 4.0f, 0.0f },  { 0.1f, 0.3f, 1.0f }, 10.0f, 30.0f);
		//renderer::submit_light({ 0.0f, 5.0f, 8.0f },  { 0.2f, 1.0f, 0.2f }, 10.0f, 30.0f);

		// --- FPS HUD ---
		fps_accum_time   += dt;
		fps_accum_frames += 1;
		if (fps_accum_time >= 1.0f) {
			f32 fps = (f32)fps_accum_frames / fps_accum_time;
			str::format(fps_text, sizeof(fps_text), "FPS: %d", (int)(fps + 0.5f));
			fps_accum_time   = 0.0f;
			fps_accum_frames = 0;
		}

		if (hud_font != renderer::INVALID_FONT) {
			// dark translucent backdrop behind the counter for readability
			renderer::draw_2d_rect(8.0f, 8.0f, 130.0f, 32.0f, { 0.0f, 0.0f, 0.0f, 0.55f });
			renderer::draw_text(hud_font, fps_text, 16.0f, 12.0f, 1.0f,
				{ 1.0f, 1.0f, 1.0f, 1.0f });
		}
	}

}
