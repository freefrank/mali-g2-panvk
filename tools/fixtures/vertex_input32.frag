#version 450
layout(location=0) in vec2 sums;
layout(location=0) out vec4 color;
void main(){color=all(equal(sums,vec2(11440,278784)))?vec4(1,0,1,1):vec4(1,0,0,1);}
