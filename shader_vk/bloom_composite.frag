#version 450
#extension GL_GOOGLE_include_directive : require

#include "blackhole_common.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D texture0;
layout(set = 0, binding = 1) uniform sampler2D texture1;

void main()
{
    fragColor = texture(texture0, uv) + texture(texture1, uv) * pc.bloomStrength;
}
