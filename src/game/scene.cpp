#include "scene.hpp"
#include "api.hpp"
#include "log.hpp"
#include "string.hpp"

namespace scene {

	static renderer::ModelHandle   helmet     = renderer::INVALID_MODEL;
	static renderer::ModelHandle   sponza = renderer::INVALID_MODEL;
	static renderer::CubemapHandle env_cubemap    = renderer::INVALID_CUBEMAP;
	static renderer::FontHandle    hud_font       = renderer::INVALID_FONT;

	// FPS smoothing — accumulate frames over a one-second window, then publish.
	static f32  fps_accum_time    = 0.0f;
	static u32  fps_accum_frames  = 0;
	static char fps_text[32]      = "FPS: --";


	bool init() {
		helmet = renderer::load_model("assets/models/damagedhelmet/DamagedHelmet.gltf");
		sponza = renderer::load_model("assets/models/sponza/Sponza.gltf");

		// directional sun: up and slightly forward/right of the origin.
		renderer::set_sun({ -0.5f, 1.0f, -0.3f }, { 1.0f, 1.0f, 1.0f }, 0.0f);
		env_cubemap = renderer::load_cubemap("whitestudio", 4.0f);
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
		renderer::unload_model(sponza);
	}

	void submit(f32 dt) {

		renderer::submit_model(helmet);
		renderer::submit_model(sponza);

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
