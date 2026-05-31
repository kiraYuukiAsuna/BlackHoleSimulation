#version 450
#extension GL_GOOGLE_include_directive : require

#include "blackhole_common.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D texture0;

vec3 aces(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    fragColor = texture(texture0, uv);
    if (pc.tonemappingEnabled > 0.5) {
        fragColor.rgb = aces(fragColor.rgb);
        fragColor.rgb = pow(fragColor.rgb, vec3(1.0 / pc.gamma));
    }
}
