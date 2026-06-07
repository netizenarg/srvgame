shader_type spatial;

uniform vec3 light_direction = vec3(0.3, 0.8, 0.5);
uniform float ambient_strength : hint_range(0.0, 1.0) = 0.1;
uniform vec3 fog_color : source_color = vec3(0.1, 0.2, 0.3);
uniform float fog_start : hint_range(0.0, 500.0) = 50.0;
uniform float fog_end : hint_range(0.0, 500.0) = 70.0;

varying float height;
varying vec3 world_pos;

void vertex() {
	height = VERTEX.y;
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float h = clamp((height + 2.0) / 6.0, 0.0, 1.0);
	vec3 green = vec3(0.3, 0.6, 0.2);
	vec3 brown = vec3(0.5, 0.4, 0.2);
	COLOR.rgb = mix(green, brown, h);
}

void fragment() {
	vec3 light_dir = normalize(light_direction);
	float intensity = max(dot(NORMAL, light_dir), 0.0);
	float ambient = ambient_strength * (0.2 + 0.8 * intensity);
	ALBEDO = COLOR.rgb * (ambient + 0.7 * intensity);
	float fog_factor = clamp((fog_end - world_pos.z) / (fog_end - fog_start), 0.0, 1.0);
	ALBEDO = mix(fog_color, ALBEDO, fog_factor);
}
