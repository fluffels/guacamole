#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location=0) in vec4 inColor;
layout(location=1) in float inLight;

layout(location=0) out vec4 outColor;

void main() {
    vec3 col = inColor.xyz + vec3(1, 1, 1);
    col /= 2;
    col *= inLight;
    outColor = vec4(col, 1);
}
