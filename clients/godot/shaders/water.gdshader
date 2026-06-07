shader_type canvas_item;

uniform vec4 water_color : source_color = vec4(0.2, 0.4, 0.8, 0.7);
uniform float wave_speed = 1.0;
uniform float wave_amplitude = 0.02;
uniform float wave_frequency = 2.0;

void fragment() {
    vec2 uv = UV;
    float wave = sin(uv.x * wave_frequency + TIME * wave_speed) * wave_amplitude;
    uv.y += wave;
    COLOR = water_color;
}
