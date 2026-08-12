#version 460 core

in float v_alpha;
in vec2 v_uv;
in vec3 v_world_pos;
in vec2 v_uv_offset;
in vec2 v_uv_scale;

uniform vec3  u_camera_pos;
uniform vec3  u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;
uniform sampler2D u_texture;

// fog of war SSBO (binding 0, same as all other shaders)
layout(std430, binding = 0) buffer FogOfWarData {
	int  fow_source_count;
	int  fow_pad[3];
	vec4 fow_sources[];
};

out vec4 frag_color;

float fow_visibility() {
	float best = 0.0;
	for (int i = 0; i < fow_source_count; i++) {
		float dx = v_world_pos.x - fow_sources[i].x;
		float dz = v_world_pos.z - fow_sources[i].y;
		float r  = fow_sources[i].z;
		float d  = sqrt(dx * dx + dz * dz);
		float v  = 1.0 - smoothstep(r * 0.85, r, d);
		best = max(best, v);
	}
	return best;
}

void main() {
	// sample texture — UVs go from (-1,-1) to (1,1), remap to (0,0)-(1,1)
	// then map into spritesheet sub-rect (single textures get offset=0,0 scale=1,1)
	vec2 base_uv = v_uv * 0.5 + 0.5;
	vec2 tex_uv = v_uv_offset + base_uv * v_uv_scale;
	vec4 tex_sample = texture(u_texture, tex_uv);
	float alpha = v_alpha * tex_sample.a;

	if (alpha < 0.001) discard;

	vec3 color = tex_sample.rgb;

	// fog of war
	float fow = mix(0.5, 1.0, fow_visibility());
	color *= fow;

	// distance fog
	float cam_dist = length(u_camera_pos - v_world_pos);
	float fog = clamp((cam_dist - u_fog_start) / (u_fog_end - u_fog_start), 0.0, 1.0);
	color = mix(color, u_fog_color, fog);

	frag_color = vec4(color, alpha);
}
