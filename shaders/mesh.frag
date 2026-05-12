#version 460 core

in vec3 v_normal;
in vec3 v_world_pos;
in vec2 v_texcoord;
in vec4 v_shadow_coord[3];

uniform vec4 u_color;
uniform vec3 u_light_dir;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_camera_pos;
uniform sampler2D u_diffuse;
uniform sampler2D u_normal_map;
uniform int u_has_texture;
uniform int u_has_normal_map;
uniform vec3  u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;
uniform float u_alpha;

uniform sampler2DShadow u_shadow_map[3];
uniform float u_shadow_splits[3];
uniform vec3  u_shadow_center;

out vec4 frag_color;

// compute TBN from screen-space derivatives (no tangent attribute needed)
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1  = dFdx(p);
    vec3 dp2  = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);

    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

float shadow_factor() {
    float dist = length(u_shadow_center.xz - v_world_pos.xz);

    for (int i = 0; i < 3; i++) {
        if (dist < u_shadow_splits[i]) {
            vec3 proj = v_shadow_coord[i].xyz / v_shadow_coord[i].w * 0.5 + 0.5;
            if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
                continue;
            return texture(u_shadow_map[i], proj);
        }
    }
    return 1.0;
}

void main() {
    vec3 n = normalize(v_normal);

    // apply normal map if present
    if (u_has_normal_map != 0) {
        vec3 map_normal = texture(u_normal_map, v_texcoord).rgb * 2.0 - 1.0;
        mat3 tbn = cotangent_frame(n, v_world_pos, v_texcoord);
        n = normalize(tbn * map_normal);
    }

    float ndl = max(dot(n, -u_light_dir), 0.0);

    vec4 base_color;
    if (u_has_texture != 0) {
        base_color = texture(u_diffuse, v_texcoord);
    } else {
        base_color = u_color;
    }

    // shadow
    float shadow = shadow_factor();

    // Blinn-Phong lighting
    vec3 ambient = u_ambient_color;
    vec3 diffuse = u_sun_color * ndl * shadow;

    vec3 view_dir = normalize(u_camera_pos - v_world_pos);
    vec3 half_dir = normalize(-u_light_dir + view_dir);
    float spec = pow(max(dot(n, half_dir), 0.0), 32.0);
    vec3 specular = u_sun_color * spec * 0.15 * shadow;

    vec3 lighting = ambient + diffuse + specular;
    vec3 lit_color = base_color.rgb * lighting;

    // distance fog
    float dist = length(u_camera_pos - v_world_pos);
    float fog  = clamp((dist - u_fog_start) / (u_fog_end - u_fog_start), 0.0, 1.0);
    frag_color = vec4(mix(lit_color, u_fog_color, fog), base_color.a * u_alpha);
}
