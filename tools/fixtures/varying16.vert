#version 450
layout(location=0) out vec4 v0; layout(location=1) out vec4 v1; layout(location=2) out vec4 v2; layout(location=3) out vec4 v3;
layout(location=4) out vec4 v4; layout(location=5) out vec4 v5; layout(location=6) out vec4 v6; layout(location=7) out vec4 v7;
layout(location=8) out vec4 v8; layout(location=9) out vec4 v9; layout(location=10) out vec4 v10; layout(location=11) out vec4 v11;
layout(location=12) out vec4 v12; layout(location=13) out vec4 v13; layout(location=14) out vec4 v14; layout(location=15) out vec4 v15;
void main() {
    float x = float((gl_VertexIndex << 1) & 2); float y = float(gl_VertexIndex & 2);
    gl_Position = vec4(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
    vec4 c = vec4(1, 0, 1, 1);
    v0=c*0.0625; v1=c*0.0625; v2=c*0.0625; v3=c*0.0625; v4=c*0.0625; v5=c*0.0625; v6=c*0.0625; v7=c*0.0625;
    v8=c*0.0625; v9=c*0.0625; v10=c*0.0625; v11=c*0.0625; v12=c*0.0625; v13=c*0.0625; v14=c*0.0625; v15=c*0.0625;
}
