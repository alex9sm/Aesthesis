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

	// one clip playing, fading in from the pose that was showing when it started. freezing
	// that pose keeps any chain of interruptions pop-free
	struct Player {
		const GltfClip* clip;
		f32  time;
		bool loop;
		bool finished;           // a one-shot reached its end and holds the last frame
		f32  fade;               // seconds into the fade
		f32  fade_length;        // 0 when not fading
		JointPose from[renderer::MAX_SKIN_JOINTS];
	};

	// restarts `clip` from 0, fading in over the current pose
	void play(Player* p, const GltfSkin& skin, const GltfClip* clip, bool loop, f32 fade_length);
	void update(Player* p, f32 dt);

	// local pose; joints the clips don't animate keep their rest pose
	void evaluate(const Player& p, const GltfSkin& skin, JointPose* out);
	// a looping sample wraps from the last key back to the first over one frame
	void sample(const GltfSkin& skin, const GltfClip& clip, f32 time, bool loop, JointPose* out);

	// model-space joint transforms
	void model_matrices(const GltfSkin& skin, const JointPose* local, mat4* out);
	// joint * inverse bind, for renderer::submit_skinned
	void skin_matrices(const GltfSkin& skin, const mat4* model, mat4* out);
	// model-space bounds of the posed mesh
	AABB bounds(const GltfSkin& skin, const mat4* model);

	i32 find_joint(const GltfSkin& skin, const char* name);
	const GltfClip* find_clip(const GltfClip* clips, u32 count, const char* name);

}
