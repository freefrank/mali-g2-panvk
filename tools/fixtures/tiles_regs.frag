#version 450
#ifndef TEX_BINDING
#define TEX_BINDING 0
#endif
#ifndef LIVE
#define LIVE 10
#endif
layout(binding = TEX_BINDING) uniform sampler2D tex;
layout(location = 0) out vec4 color;
void main() {
   ivec2 p = ivec2(gl_FragCoord.xy);
   vec4 v[LIVE];
   for (int i = 0; i < LIVE; i++)
      v[i] = texelFetch(tex, p + ivec2(i * 3 - 7, i * 5 - 11), 0) * float(i + 1);
   vec4 r = vec4(0.0);
   for (int i = 0; i < LIVE; i++)
      r = r * 0.5 + v[(i * 7) % LIVE] * v[(i * 3 + 1) % LIVE];
   color = r;
}
