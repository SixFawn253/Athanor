#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 dimensions;
    vec2 cornerRadii;
    vec2 bottomRadii;
};
layout(binding=1) uniform sampler2D source;
void main() {
    vec2 p=qt_TexCoord0*dimensions;
    vec2 radii=p.y<dimensions.y*.5 ? cornerRadii : bottomRadii;
    float radius=p.x<dimensions.x*.5 ? radii.x : radii.y;
    vec2 q=abs(p-dimensions*.5)-dimensions*.5+vec2(radius);
    float distance=length(max(q,vec2(0)))+min(max(q.x,q.y),0)-radius;
    fragColor=texture(source,qt_TexCoord0)*(1-smoothstep(-.5,.5,distance))*qt_Opacity;
}
