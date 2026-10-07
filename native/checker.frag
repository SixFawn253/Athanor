#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 dimensions;
    vec4 colorA;
    vec4 colorB;
};
void main() {
    vec2 p=qt_TexCoord0*dimensions;
    vec2 tile=floor(p/12.0);
    vec4 color=mod(tile.x+tile.y,2.0)<1.0 ? colorA : colorB;
    vec2 q=abs(p-dimensions*.5)-dimensions*.5+vec2(12.0);
    float d=length(max(q,vec2(0)))+min(max(q.x,q.y),0)-12.0;
    fragColor=color*(1.0-smoothstep(-.5,.5,d))*qt_Opacity;
}
