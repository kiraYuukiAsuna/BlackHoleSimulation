#version 450
#extension GL_GOOGLE_include_directive : require

#include "blackhole_common.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D texture0;
layout(set = 0, binding = 1) uniform sampler2D texture1;

void main()
{
    vec2 inputTexelSize = 1.0 / pc.resolution * 0.5;
    vec4 o = inputTexelSize.xyxy * vec4(-1.0, -1.0, 1.0, 1.0);
    fragColor = 0.25 * (texture(texture0, uv + o.xy) + texture(texture0, uv + o.zy) +
                        texture(texture0, uv + o.xw) + texture(texture0, uv + o.zw));
    fragColor += texture(texture1, uv);
    fragColor.a = 1.0;
}
