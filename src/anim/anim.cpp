#include "anim.hpp"
#include "string.hpp"

namespace anim {

	using renderer::GltfChannel;
	using renderer::ChannelPath;

	static vec3 lerp(vec3 a, vec3 b, f32 u) { return a + (b - a) * u; }

	// shortest-arc normalized lerp; keys are a frame apart, so it matches slerp closely
	static quat nlerp(quat a, quat b, f32 u) {
		if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) b = { -b.x, -b.y, -b.z, -b.w };
		return quat_normalize({ a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
			a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u });
	}

	static vec3 key3(const f32* v, u32 k) { return { v[k * 3], v[k * 3 + 1], v[k * 3 + 2] }; }
	static quat key4(const f32* v, u32 k) { return { v[k * 4], v[k * 4 + 1], v[k * 4 + 2], v[k * 4 + 3] }; }

	static void rest_pose(const GltfSkin& skin, JointPose* out) {
		for (u32 k = 0; k < skin.joint_count; k++) out[k] = { skin.rest_t[k], skin.rest_r[k], skin.rest_s[k] };
	}

	void sample(const GltfSkin& skin, const GltfClip& clip, f32 time, bool loop, JointPose* out) {
		rest_pose(skin, out);
		for (u32 c = 0; c < clip.channel_count; c++) {
			const GltfChannel& ch = clip.channels[c];
			u32 n = ch.key_count;
			if (n == 0 || ch.joint >= skin.joint_count) continue;

			// keys a and b bracket `time`
			u32 a = 0, b = 0;
			f32 u = 0.0f;
			if (time >= ch.times[n - 1]) {
				a = b = n - 1;
				if (loop && clip.frame_time > 0.0f && !ch.step) {
					b = 0;
					u = (time - ch.times[n - 1]) / clip.frame_time;
					if (u > 1.0f) u = 1.0f;
				}
			} else if (time > ch.times[0]) {
				u32 lo = 0, hi = n - 1;
				while (hi - lo > 1) {
					u32 mid = (lo + hi) / 2;
					if (ch.times[mid] <= time) lo = mid; else hi = mid;
				}
				a = lo;
				b = hi;
				f32 span = ch.times[b] - ch.times[a];
				u = (ch.step || span <= 0.0f) ? 0.0f : (time - ch.times[a]) / span;
			}

			JointPose& p = out[ch.joint];
			switch (ch.path) {
			case ChannelPath::Translation: p.t = lerp(key3(ch.values, a), key3(ch.values, b), u); break;
			case ChannelPath::Rotation:    p.r = nlerp(key4(ch.values, a), key4(ch.values, b), u); break;
			case ChannelPath::Scale:       p.s = lerp(key3(ch.values, a), key3(ch.values, b), u); break;
			}
		}
	}

	void play(Player* p, const GltfSkin& skin, const GltfClip* clip, bool loop, f32 fade_length) {
		p->fade = 0.0f;
		p->fade_length = 0.0f;
		if (p->clip && fade_length > 0.0f) {
			// `from` may be mid-fade itself, so evaluate into scratch first
			JointPose now[renderer::MAX_SKIN_JOINTS];
			evaluate(*p, skin, now);
			for (u32 k = 0; k < skin.joint_count; k++) p->from[k] = now[k];
			p->fade_length = fade_length;
		}
		p->clip     = clip;
		p->time     = 0.0f;
		p->loop     = loop;
		p->finished = false;
	}

	void update(Player* p, f32 dt) {
		if (!p->clip) return;
		p->time += dt;
		if (p->loop) {
			f32 period = p->clip->duration + p->clip->frame_time;
			p->time = period > 0.0f ? p->time - math::floor(p->time / period) * period : 0.0f;
		} else if (p->time >= p->clip->duration) {
			p->time = p->clip->duration;
			p->finished = true;
		}
		if (p->fade_length > 0.0f) {
			p->fade += dt;
			if (p->fade >= p->fade_length) p->fade_length = 0.0f;
		}
	}

	void evaluate(const Player& p, const GltfSkin& skin, JointPose* out) {
		if (!p.clip) rest_pose(skin, out);
		else sample(skin, *p.clip, p.time, p.loop, out);
		if (p.fade_length <= 0.0f) return;

		f32 w = p.fade / p.fade_length;
		w = w * w * (3.0f - 2.0f * w);   // ease in and out
		for (u32 k = 0; k < skin.joint_count; k++) {
			out[k].t = lerp(p.from[k].t, out[k].t, w);
			out[k].r = nlerp(p.from[k].r, out[k].r, w);
			out[k].s = lerp(p.from[k].s, out[k].s, w);
		}
	}

	void model_matrices(const GltfSkin& skin, const JointPose* local, mat4* out) {
		for (u32 k = 0; k < skin.joint_count; k++) {
			mat4 m = mat4_translate(local[k].t) * quat_to_mat4(local[k].r) * mat4_scale(local[k].s);
			out[k] = (skin.parents[k] < 0 ? skin.root : out[skin.parents[k]]) * m;
		}
	}

	void skin_matrices(const GltfSkin& skin, const mat4* model, mat4* out) {
		for (u32 k = 0; k < skin.joint_count; k++) out[k] = model[k] * skin.inverse_bind[k];
	}

	AABB bounds(const GltfSkin& skin, const mat4* model) {
		vec3 lo = { 1e30f, 1e30f, 1e30f };
		vec3 hi = { -1e30f, -1e30f, -1e30f };
		for (u32 k = 0; k < skin.joint_count; k++) {
			vec3 p = { model[k].col[3][0], model[k].col[3][1], model[k].col[3][2] };
			if (p.x < lo.x) lo.x = p.x; if (p.x > hi.x) hi.x = p.x;
			if (p.y < lo.y) lo.y = p.y; if (p.y > hi.y) hi.y = p.y;
			if (p.z < lo.z) lo.z = p.z; if (p.z > hi.z) hi.z = p.z;
		}
		vec3 m = { skin.bounds_margin, skin.bounds_margin, skin.bounds_margin };
		return { lo - m, hi + m };
	}

	i32 find_joint(const GltfSkin& skin, const char* name) {
		for (u32 k = 0; k < skin.joint_count; k++) {
			if (str::equal(skin.names[k], name)) return (i32)k;
		}
		return -1;
	}

	const GltfClip* find_clip(const GltfClip* clips, u32 count, const char* name) {
		for (u32 i = 0; i < count; i++) {
			if (str::equal(clips[i].name, name)) return &clips[i];
		}
		return nullptr;
	}

}
