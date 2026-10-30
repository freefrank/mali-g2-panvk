#version 450
#ifndef TEX_BINDING
#define TEX_BINDING 0
#endif
layout(binding = TEX_BINDING) uniform sampler2D tex;
layout(location = 0) out vec4 color;
void main() {
   color = texelFetch(tex, ivec2(gl_FragCoord.xy), 0);
   if (color.a < 0.5) discard;
}
