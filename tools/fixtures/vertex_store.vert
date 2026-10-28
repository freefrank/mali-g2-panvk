#version 450
layout(set = 0, binding = 0, std430) buffer Results {
    uint mask;
    uint values[3];
} results;
void main() {
    uint i = uint(gl_VertexIndex);
    if (i < 3u) {
        results.values[i] = i * i + 7u;
        atomicOr(results.mask, 1u << i);
    }
    float x = float((gl_VertexIndex << 1) & 2);
    float y = float(gl_VertexIndex & 2);
    gl_Position = vec4(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
}
