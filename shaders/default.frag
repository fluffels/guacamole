#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location=0) in vec4 inNormal;

layout(location=0) out vec4 outColor;

void main() {
    vec3 col = inNormal.xyz + vec3(1, 1, 1);
    col /= 2;
    outColor = vec4(col, 1);
}
