#version 450
#extension GL_ARB_shader_draw_parameters : require
layout(location = 0) flat out uint draw_id;
void main() {
    float x = float((gl_VertexIndex << 1) & 2);
    float y = float(gl_VertexIndex & 2);
    gl_Position = vec4(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
    draw_id = gl_DrawIDARB;
}
