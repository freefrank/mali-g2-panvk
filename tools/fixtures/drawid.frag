#version 450
layout(location = 0) flat in uint draw_id;
layout(location = 0) out vec4 color;
void main() {
    if ((gl_FragCoord.x < 8.0) != (draw_id == 0u))
        discard;
    color = draw_id == 0u ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
}
