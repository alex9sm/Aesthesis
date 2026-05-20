// Octahedral unit-vector encoding/decoding (Cigolle et al.).
// Encodes a 3D unit vector into two signed values in [-1, 1] suitable for
// storage in an RG16F G-buffer attachment. Decoding reconstructs the unit
// vector with practically lossless quality at this precision.

vec2 oct_sign_not_zero(vec2 v) {
	return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec2 encode_octahedral(vec3 n) {
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	vec2 e = (n.z >= 0.0) ? n.xy : (1.0 - abs(n.yx)) * oct_sign_not_zero(n.xy);
	return e;
}

vec3 decode_octahedral(vec2 e) {
	vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * oct_sign_not_zero(n.xy);
	return normalize(n);
}
