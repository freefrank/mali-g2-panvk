#version 450
layout(location = 0) out vec4 color;
void main() {
    color = vec4(vec2(ivec2(gl_FragCoord.xy)) / 15.0,
                 gl_FrontFacing ? 1.0 : 0.0, gl_FragCoord.w);
}
