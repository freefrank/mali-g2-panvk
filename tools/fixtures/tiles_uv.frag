#version 450
#ifndef TEX_BINDING
#define TEX_BINDING 0
#endif
layout(binding = TEX_BINDING) uniform sampler2D tex;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
   color = texture(tex, uv);
}
