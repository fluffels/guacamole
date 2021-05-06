#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding=0) uniform sampler2D colorMap;

layout(location=0) in vec2 inST;

layout(location=0) out vec4 outColor;

void main() {
    outColor = vec4(inST.s, inST.t, 0, 1);
}
