#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "quaternions.glsl"
#include "uniforms.glsl"

layout(location=0) in vec3 inPosition;

void main() {
    gl_Position = vec4(inPosition, 1);
}
