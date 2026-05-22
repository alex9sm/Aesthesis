#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in  vec2 v_uv;
layout(location = 1) in  vec4 v_color;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 3) uniform sampler2D textures[256];

layout(push_constant) uniform Push {
	uint atlas_idx;
} pc;

void main() {
	// SDF replicated in all channels; sample .r as the distance value
	float d = texture(textures[nonuniformEXT(pc.atlas_idx)], v_uv).r;

	// smooth threshold at 0.5 with screen-space derivative for crisp edges
	float w = fwidth(d);
	float a = smoothstep(0.5 - w, 0.5 + w, d);

	out_color = vec4(v_color.rgb, v_color.a * a);
}
