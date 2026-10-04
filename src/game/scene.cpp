#include "scene.hpp"
#include "api.hpp"
#include "gltf.hpp"
#include "physics.hpp"
#include "log.hpp"
#include "string.hpp"
#include "math.hpp"

namespace scene {

	static renderer::ModelHandle   helmet     = renderer::INVALID_MODEL;
	static renderer::ModelHandle   chess = renderer::INVALID_MODEL;
	static renderer::ModelHandle room = renderer::INVALID_MODEL;
	static renderer::CubemapHandle env_cubemap    = renderer::INVALID_CUBEMAP;
	static renderer::FontHandle    hud_font       = renderer::INVALID_FONT;

	static constexpr u32 MAX_ROOM_BODIES = 64;
	static physics::ObjectBody room_bodies[MAX_ROOM_BODIES];
	static u32                 room_body_count = 0;

	// FPS smoothing — accumulate frames over a one-second window, then publish.
	static f32  fps_accum_time    = 0.0f;
	static u32  fps_accum_frames  = 0;
	static char fps_text[32]      = "FPS: --";

	static f32  light_time        = 0.0f;


	bool init() {
		helmet = renderer::load_model("assets/models/damagedhelmet/DamagedHelmet.gltf");
		//chess = renderer::load_model("assets/models/chess/chess.gltf");

		renderer::GltfModel room_gltf = {};
		if (renderer::load_gltf_model("assets/models/testroom/testingroom.gltf", &room_gltf)) {
			room = renderer::load_model(room_gltf);
			room_body_count = physics::create_bodies(room_gltf, mat4_identity(), room_bodies, MAX_ROOM_BODIES);
			renderer::free_gltf_model(&room_gltf);
		}

		renderer::set_sun({ 0.38f, 1.0f, 0.41f }, { 1.0f, 1.0f, 1.0f }, 0.0f);
		env_cubemap = renderer::load_cubemap("night", 1.0f);
		renderer::set_environment_cubemap(env_cubemap);

		hud_font = renderer::load_font("assets/textures/global/NeueHaasDisplayMediu.ttf", 24.0f);
		if (hud_font == renderer::INVALID_FONT) {
			logger::error("Failed to load HUD font");
		}
		return true;
	}

	void shutdown() {
		for (u32 i = 0; i < room_body_count; i++) physics::destroy_body(room_bodies[i].body);
		room_body_count = 0;
		renderer::unload_font(hud_font);
		renderer::clear_environment_cubemap();
		renderer::unload_cubemap(env_cubemap);
		renderer::unload_model(helmet);
		//renderer::unload_model(chess);
		renderer::unload_model(room);
	}

	void submit(f32 dt) {

		renderer::submit_model(helmet, mat4_translate({ 0.0f, 3.0f, 0.0f }));
		//renderer::submit_model(chess);

		renderer::ObjectOverride room_overrides[MAX_ROOM_BODIES];
		u32 room_override_count = 0;
		for (u32 i = 0; i < room_body_count; i++) {
			const physics::ObjectBody& ob = room_bodies[i];
			if (!ob.dynamic) continue;
			vec3 pos;
			quat rot;
			physics::get_transform(ob.body, &pos, &rot);
			room_overrides[room_override_count++] = { ob.object_index, mat4_from_pos_rot(pos, rot) * mat4_scale(ob.scale) };
		}
		renderer::submit_model(room, mat4_identity(), { 1.0f, 1.0f, 1.0f, 1.0f }, room_overrides, room_override_count);

		// test point lights
		// renderer::submit_point_light({ 2.0f, 2.0f, 1.0f },  { 1.0f, 0.3f, 0.1f }, 10.0f, 1000.0f);
		// renderer::submit_point_light({-4.0f, 4.0f, 0.0f },  { 0.1f, 0.3f, 1.0f }, 10.0f, 1000.0f);
		// renderer::submit_point_light({ 0.0f, 5.0f, 8.0f },  { 0.2f, 1.0f, 0.2f }, 10.0f, 1000.0f);

		renderer::submit_spot_light({ -14.0f, 6.0f, 2.0f }, { 1.0f, 0.8f, 0.55f }, 14.0f, 1000.0f,
			0.1f, { 0.0f, -1.0f, 0.0f }, 5.0f, 40.0f, true);
		renderer::submit_spot_light({ -14.0f, 2.0f, 8.0f }, { 1.0f, 0.8f, 0.55f }, 20.0f, 1000.0f,
			0.1f, { 1.0f, -0.6f, 0.6f }, 5.0f, 60.0f, true);
		renderer::submit_spot_light({ 12.0f, 12.0f, -6.0f }, { 1.0f, 0.8f, 0.55f }, 20.0f, 1000.0f,
			0.1f, { 0.0f, -1.0f, 0.0f }, 5.0f, 60.0f, true);
		renderer::submit_spot_light({ 12.0f, 5.0f, -17.0f }, { 1.0f, 0.8f, 0.55f }, 30.0f, 1000.0f,
			0.1f, { 0.0f, -1.0f, 0.0f }, 5.0f, 60.0f, false);


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
