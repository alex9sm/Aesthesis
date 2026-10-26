#include "viewmodel.hpp"
#include "anim.hpp"
#include "api.hpp"
#include "assets.hpp"
#include "log.hpp"

namespace viewmodel {

	using renderer::GltfClip;

	// tuning. the defaults reproduce the Blender camera's framing in sidearm.blend
	static constexpr vec3 EYE       = { 0.0f, 0.6f, 0.0f };   // eye position in model space (glTF, +Y up)
	static constexpr f32  YAW       = -90.0f;   // degrees; turns the model's forward (-X) to view forward (-Z)
	static constexpr f32  FOV_Y     = 45.0f;    // degrees; the viewmodel's own vertical FOV
	static constexpr f32  DEPTH     = 0.2f;     // view-space depth squash, keeps it nearer than any wall
	static constexpr f32  FIRE_FADE   = 0.03f;
	static constexpr f32  ACTION_FADE = 0.1f;    // into reload / draw / stow
	static constexpr f32  IDLE_FADE   = 0.12f;   // back to idle or walk after an action
	static constexpr f32  WALK_FADE   = 0.2f;    // idle <-> walk

	// idle breathing: the whole viewmodel rises and tips up slightly on each inhale
	static constexpr f32  BREATH_RATE  = 0.25f;    // breaths per second
	static constexpr f32  BREATH_LIFT  = 0.002f;   // model units
	static constexpr f32  BREATH_PITCH = 0.1f;     // degrees

	static assets::ModelRef         ref        = assets::INVALID_MODEL_REF;
	static const renderer::GltfSkin* skin      = nullptr;
	static const GltfClip*          clips      = nullptr;
	static u32                      clip_count = 0;
	static renderer::SkinnedHandle  instance   = renderer::INVALID_SKINNED;
	static anim::Player             player     = {};
	static f32                      breath     = 0.0f;   // seconds into the current breath

	// Ready plays idle or walk; the rest play their one-shot clip
	enum class State : u8 { Ready, Firing, Reloading, Stowing, Stowed, Drawing };
	static State state   = State::Ready;
	static bool  walking = false;

	static struct {
		const GltfClip* idle;
		const GltfClip* walk;
		const GltfClip* fire;
		const GltfClip* reload;
		const GltfClip* draw;
		const GltfClip* stow;
	} clip = {};

	// a missing clip leaves the state unchanged
	static bool start(State s, const GltfClip* c, bool loop, f32 fade) {
		if (!c) return false;
		anim::play(&player, *skin, c, loop, fade);
		state = s;
		return true;
	}

	static void start_ready(bool walk, f32 fade) {
		walking = walk;
		start(State::Ready, walk && clip.walk ? clip.walk : clip.idle, true, fade);
	}

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

		const char* names[] = { "idle", "walk", "fire", "reload", "draw", "stow" };
		const GltfClip** slots[] = { &clip.idle, &clip.walk, &clip.fire, &clip.reload, &clip.draw, &clip.stow };
		for (u32 i = 0; i < 6; i++) {
			*slots[i] = anim::find_clip(clips, clip_count, names[i]);
			if (!*slots[i]) logger::warn("viewmodel '%s': no '%s' clip", name, names[i]);
		}
		player = {};
		start_ready(false, 0.0f);
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
		clip = {};
		player = {};
		state = State::Ready;
	}

	bool update(f32 dt, const Input& in) {
		if (!skin) return false;
		bool fired = false;
		bool armed = state == State::Ready || state == State::Firing;

		if (in.toggle) {
			bool stowing = state == State::Stowed || state == State::Stowing;
			// from hidden the draw starts off-screen, so there's nothing to fade from
			if (stowing && !start(State::Drawing, clip.draw, false, state == State::Stowed ? 0.0f : ACTION_FADE)) state = State::Ready;
			if (!stowing && !start(State::Stowing, clip.stow, false, ACTION_FADE)) state = State::Stowed;
		} else if (in.reload && armed) {
			start(State::Reloading, clip.reload, false, ACTION_FADE);
		} else if (in.fire && armed) {
			fired = start(State::Firing, clip.fire, false, FIRE_FADE);
		}
		if (state == State::Ready && in.walking != walking) start_ready(in.walking, WALK_FADE);

		anim::update(&player, dt);
		if (player.finished) {
			if (state == State::Stowing) state = State::Stowed;
			else if (state != State::Ready && state != State::Stowed) start_ready(in.walking, IDLE_FADE);
		}

		breath += dt;
		if (breath >= 1.0f / BREATH_RATE) breath -= 1.0f / BREATH_RATE;
		return fired;
	}

	void submit(const mat4& view, f32 fov_y) {
		if (!skin || state == State::Stowed) return;
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
