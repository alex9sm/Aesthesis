#include "viewmodel.hpp"
#include "anim.hpp"
#include "api.hpp"
#include "assets.hpp"
#include "log.hpp"

namespace viewmodel {

	using renderer::GltfClip;

	// tuning. the defaults reproduce the Blender camera's framing in sidearm.blend
	static constexpr vec3 EYE       = { 0.0f, 0.62f, 0.0f };   // eye position in model space (glTF, +Y up)
	static constexpr f32  YAW       = -90.0f;   // degrees; turns the model's forward (-X) to view forward (-Z)
	static constexpr f32  FOV_Y     = 50.0f;    // degrees; the viewmodel's own vertical FOV
	static constexpr f32  DEPTH     = 0.2f;     // view-space depth squash, keeps it nearer than any wall
	static constexpr f32  FIRE_FADE = 0.03f;
	static constexpr f32  IDLE_FADE = 0.1f;

	// idle breathing: the whole viewmodel rises and tips up slightly on each inhale
	static constexpr f32  BREATH_RATE  = 0.25f;    // breaths per second
	static constexpr f32  BREATH_LIFT  = 0.002f;   // model units
	static constexpr f32  BREATH_PITCH = 0.1f;     // degrees

	static assets::ModelRef         ref        = assets::INVALID_MODEL_REF;
	static const renderer::GltfSkin* skin      = nullptr;
	static const GltfClip*          clips      = nullptr;
	static u32                      clip_count = 0;
	static renderer::SkinnedHandle  instance   = renderer::INVALID_SKINNED;
	static const GltfClip*          idle       = nullptr;
	static const GltfClip*          fire_clip  = nullptr;
	static anim::Player             player     = {};
	static f32                      breath     = 0.0f;   // seconds into the current breath

	bool load(const char* name) {
		ref = assets::acquire_model(name);
		if (ref == assets::INVALID_MODEL_REF) return false;
		skin = assets::skin(ref);
		if (!skin) {
			logger::error("viewmodel '%s' has no skin", name);
			unload();
			return false;
		}
		clips = assets::clips(ref, &clip_count);
		instance = renderer::create_skinned(assets::render_model(ref));

		idle = anim::find_clip(clips, clip_count, "idle");
		fire_clip = anim::find_clip(clips, clip_count, "fire");
		if (!idle || !fire_clip) logger::warn("viewmodel '%s': missing 'idle' or 'fire' clip", name);
		player = {};
		anim::play(&player, idle, true, 0.0f);
		return true;
	}

	void unload() {
		renderer::destroy_skinned(instance);
		assets::release_model(ref);
		ref = assets::INVALID_MODEL_REF;
		skin = nullptr;
		clips = nullptr;
		clip_count = 0;
		instance = renderer::INVALID_SKINNED;
		idle = fire_clip = nullptr;
		player = {};
	}

	void update(f32 dt, bool fire) {
		if (!skin) return;
		if (fire && fire_clip) anim::play(&player, fire_clip, false, FIRE_FADE);
		else if (player.finished && player.clip == fire_clip) anim::play(&player, idle, true, IDLE_FADE);
		anim::update(&player, dt);

		breath += dt;
		if (breath >= 1.0f / BREATH_RATE) breath -= 1.0f / BREATH_RATE;
	}

	void submit(const mat4& view, f32 fov_y) {
		if (!skin) return;
		anim::JointPose pose[renderer::MAX_SKIN_JOINTS];
		mat4 joints[renderer::MAX_SKIN_JOINTS];
		mat4 palette[renderer::MAX_SKIN_JOINTS];
		anim::evaluate(player, *skin, pose);
		anim::model_matrices(*skin, pose, joints);
		anim::skin_matrices(*skin, joints, palette);
		AABB bounds = anim::bounds(*skin, joints);

		// breathing pivots on the eye, applied once the model faces view forward
		f32 b = math::sin(TAU * BREATH_RATE * breath);
		mat4 breathe = mat4_translate({ 0.0f, BREATH_LIFT * b, 0.0f })
			* mat4_rotate(to_radians(BREATH_PITCH * b), { 1.0f, 0.0f, 0.0f });

		// in view space: x/y scaled by k gives the viewmodel its own FOV, z squashed by DEPTH.
		// one matrix, so deferred lighting and culling see an ordinary mesh
		f32 k = DEPTH * math::tan(fov_y * 0.5f) / math::tan(to_radians(FOV_Y) * 0.5f);
		mat4 local = breathe * mat4_rotate(to_radians(YAW), { 0.0f, 1.0f, 0.0f }) * mat4_translate(-EYE);
		mat4 world = mat4_inverse(view) * mat4_scale(vec3{ k, k, DEPTH }) * local;
		renderer::submit_skinned(instance, world, palette, skin->joint_count, bounds, false);
	}

}
