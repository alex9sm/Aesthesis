#pragma once

#include "types.hpp"
#include "math.hpp"
#include "gltf.hpp"

// CPU skeletal animation: clip sampling, crossfade, joint matrices. backend-agnostic
namespace anim {

	using renderer::GltfSkin;
	using renderer::GltfClip;

	struct JointPose {
		vec3 t;
		quat r;
		vec3 s;
	};

	// one clip playing, optionally fading in over the one before it
	struct Player {
		const GltfClip* clip;
		const GltfClip* prev;    // fading out, null when none
		f32  time;
		f32  prev_time;
		f32  fade;               // seconds into the fade
		f32  fade_length;
		bool loop;
		bool prev_loop;
		bool finished;           // a one-shot reached its end and holds the last frame
	};

	// restarts `clip` from 0, fading in over whatever was playing
	void play(Player* p, const GltfClip* clip, bool loop, f32 fade_length = 0.1f);
	void update(Player* p, f32 dt);

	// local pose; joints the clips don't animate keep their rest pose
	void evaluate(const Player& p, const GltfSkin& skin, JointPose* out);
	void sample(const GltfSkin& skin, const GltfClip& clip, f32 time, JointPose* out);

	// model-space joint transforms
	void model_matrices(const GltfSkin& skin, const JointPose* local, mat4* out);
	// joint * inverse bind, for renderer::submit_skinned
	void skin_matrices(const GltfSkin& skin, const mat4* model, mat4* out);
	// model-space bounds of the posed mesh
	AABB bounds(const GltfSkin& skin, const mat4* model);

	i32 find_joint(const GltfSkin& skin, const char* name);
	const GltfClip* find_clip(const GltfClip* clips, u32 count, const char* name);

}
