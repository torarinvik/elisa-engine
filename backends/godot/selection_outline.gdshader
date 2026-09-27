shader_type spatial;

render_mode unshaded, cull_front;

uniform vec4 outline_color : source_color = vec4(1.0, 0.55, 0.08, 1.0);
uniform float outline_width = 0.035;

void vertex() {
	VERTEX += NORMAL * outline_width;
}

void fragment() {
	ALBEDO = outline_color.rgb;
	EMISSION = outline_color.rgb;
}
