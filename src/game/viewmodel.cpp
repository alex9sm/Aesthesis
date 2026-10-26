#include "viewmodel.hpp"
#include "anim.hpp"
#include "api.hpp"
#include "gltf.hpp"
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

	static renderer::GltfSkin*     skin       = nullptr;
	static GltfClip*               clips      = nullptr;
	static u32                     clip_count = 0;
	static renderer::ModelHandle   model      = renderer::INVALID_MODEL;
	static renderer::SkinnedHandle instance   = renderer::INVALID_SKINNED;
	static const GltfClip*         idle       = nullptr;
	static const GltfClip*         fire_clip  = nullptr;
	static anim::Player            player     = {};

	bool load(const char* path) {
		renderer::GltfModel gm = {};
		if (!renderer::load_gltf_model(path, &gm)) return false;
		if (!gm.skin) {
			logger::error("viewmodel '%s' has no skin", path);
			renderer::free_gltf_model(&gm);
			return false;
		}

		model = renderer::load_model(gm);
		skin = gm.skin;
		clips = gm.clips;
		clip_count = gm.clip_count;
		gm.skin = nullptr;
		gm.clips = nullptr;
		gm.clip_count = 0;
		renderer::free_gltf_model(&gm);

		instance = renderer::create_skinned(model);
		idle = anim::find_clip(clips, clip_count, "idle");
		fire_clip = anim::find_clip(clips, clip_count, "fire");
		if (!idle || !fire_clip) logger::warn("viewmodel '%s': missing 'idle' or 'fire' clip", path);
		player = {};
		anim::play(&player, idle, true, 0.0f);
		return true;
	}

	void unload() {
		renderer::destroy_skinned(instance);
		renderer::unload_model(model);
		renderer::free_gltf_skin(skin);
		renderer::free_gltf_clips(clips, clip_count);
		skin = nullptr;
		clips = nullptr;
		clip_count = 0;
		model = renderer::INVALID_MODEL;
		instance = renderer::INVALID_SKINNED;
		idle = fire_clip = nullptr;
		player = {};
	}

	void update(f32 dt, bool fire) {
		if (!skin) return;
		if (fire && fire_clip) anim::play(&player, fire_clip, false, FIRE_FADE);
		else if (player.finished && player.clip == fire_clip) anim::play(&player, idle, true, IDLE_FADE);
		anim::update(&player, dt);
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

		// in view space: x/y scaled by k gives the viewmodel its own FOV, z squashed by DEPTH.
		// one matrix, so deferred lighting and culling see an ordinary mesh
		f32 k = DEPTH * math::tan(fov_y * 0.5f) / math::tan(to_radians(FOV_Y) * 0.5f);
		mat4 local = mat4_rotate(to_radians(YAW), { 0.0f, 1.0f, 0.0f }) * mat4_translate(-EYE);
		mat4 world = mat4_inverse(view) * mat4_scale(vec3{ k, k, DEPTH }) * local;
		renderer::submit_skinned(instance, world, palette, skin->joint_count, bounds, false);
	}

}
