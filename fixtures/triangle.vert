#version 450
void main() {
    float x = float((gl_VertexIndex << 1) & 2);
    float y = float(gl_VertexIndex & 2);
    gl_Position = vec4(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
}
