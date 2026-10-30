#version 450
#ifndef TEX_BINDING
#define TEX_BINDING 2
#endif
layout(binding = TEX_BINDING) uniform sampler2D tex;
layout(binding = 1, std140) uniform Params { vec4 tint; } pc;
#ifdef USE_UV
layout(location = 0) in vec2 uv;
#endif
layout(location = 0) out vec4 color;
void main() {
#ifdef USE_UV
   vec4 c = texture(tex, uv);
#else
   vec4 c = texelFetch(tex, ivec2(gl_FragCoord.xy), 0);
#endif
   color = vec4(mix(c.rgb, pc.tint.rgb, pc.tint.a), c.a);
}
