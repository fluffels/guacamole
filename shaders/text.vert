#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location=0) in vec4 inXYZ;
layout(location=1) in vec2 inST;

layout(location=0) out vec2 outST;

void main() {
    gl_Position = vec4(inXYZ.xyz, 1.f);
    outST = inST;
}
