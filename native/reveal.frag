#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
 mat4 qt_Matrix;
 float qt_Opacity;
 vec4 coverColor;
 vec2 dimensions;
 float progress;
};
void main() {
 vec2 p=(qt_TexCoord0-vec2(0.5))*dimensions;
 float radius=length(dimensions*0.5)*progress;
 float alpha=smoothstep(radius-1.0,radius+1.0,length(p));
 fragColor=vec4(coverColor.rgb*alpha,alpha)*qt_Opacity;
}
