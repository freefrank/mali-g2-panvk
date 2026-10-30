#version 450
#ifndef TEX_BINDING
#define TEX_BINDING 0
#endif
layout(binding = TEX_BINDING) uniform sampler2D tex;
layout(location = 0, index = 0) out vec4 color;
layout(location = 0, index = 1) out vec4 secondary;
void main() {
   color = texelFetch(tex, ivec2(gl_FragCoord.xy), 0);
   secondary = vec4(color.a);
}
