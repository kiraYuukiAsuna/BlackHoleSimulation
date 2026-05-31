#version 450
#extension GL_GOOGLE_include_directive : require

#include "blackhole_common.glsl"

const float PI = 3.14159265359;
const float EPSILON = 0.0001;

layout(location = 0) in vec2 screenUv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 2) uniform sampler2D colorMap;
layout(set = 0, binding = 3) uniform samplerCube galaxy;

vec4 permute(vec4 x) { return mod(((x * 34.0) + 1.0) * x, 289.0); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec3 v)
{
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);
    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + 2.0 * C.xxx;
    vec3 x3 = x0 - 1.0 + 3.0 * C.xxx;
    i = mod(i, 289.0);
    vec4 p = permute(permute(permute(i.z + vec4(0.0, i1.z, i2.z, 1.0)) + i.y +
                             vec4(0.0, i1.y, i2.y, 1.0)) +
                     i.x + vec4(0.0, i1.x, i2.x, 1.0));
    float n_ = 1.0 / 7.0;
    vec3 ns = n_ * D.wyz - D.xzx;
    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

vec3 accel(float h2, vec3 pos)
{
    float r2 = dot(pos, pos);
    float r5 = pow(r2, 2.5);
    return -1.5 * h2 * pos / r5;
}

vec4 quatFromAxisAngle(vec3 axis, float angle)
{
    float half_angle = angle * 0.5 * PI / 180.0;
    return vec4(axis * sin(half_angle), cos(half_angle));
}

vec4 quatConj(vec4 q) { return vec4(-q.x, -q.y, -q.z, q.w); }

vec4 quatMult(vec4 a, vec4 b)
{
    return vec4((a.w * b.x) + (a.x * b.w) + (a.y * b.z) - (a.z * b.y),
                (a.w * b.y) - (a.x * b.z) + (a.y * b.w) + (a.z * b.x),
                (a.w * b.z) + (a.x * b.y) - (a.y * b.x) + (a.z * b.w),
                (a.w * b.w) - (a.x * b.x) - (a.y * b.y) - (a.z * b.z));
}

vec3 rotateVector(vec3 position, vec3 axis, float angle)
{
    vec4 qr = quatFromAxisAngle(axis, angle);
    return quatMult(quatMult(qr, vec4(position, 0.0)), quatConj(qr)).xyz;
}

vec3 toSpherical(vec3 p)
{
    float rho = length(p);
    float theta = atan(p.z, p.x);
    float phi = asin(p.y / rho);
    return vec3(rho, theta, phi);
}

mat3 lookAt(vec3 origin, vec3 target, float roll)
{
    vec3 rr = vec3(sin(roll), cos(roll), 0.0);
    vec3 ww = normalize(target - origin);
    vec3 uu = normalize(cross(ww, rr));
    vec3 vv = normalize(cross(uu, ww));
    return mat3(uu, vv, ww);
}

void adiskColor(vec3 pos, inout vec3 color, inout float alpha)
{
    float innerRadius = 2.6;
    float outerRadius = 12.0;
    float density = max(0.0, 1.0 - length(pos.xyz / vec3(outerRadius, pc.adiskHeight, outerRadius)));
    if (density < 0.001)
        return;

    density *= pow(1.0 - abs(pos.y) / max(pc.adiskHeight, 0.001), pc.adiskDensityV);
    density *= smoothstep(innerRadius, innerRadius * 1.1, length(pos));
    if (density < 0.001)
        return;

    vec3 sphericalCoord = toSpherical(pos);
    sphericalCoord.y *= 2.0;
    sphericalCoord.z *= 4.0;
    density *= 1.0 / pow(sphericalCoord.x, pc.adiskDensityH);
    density *= 16000.0;

    if (pc.adiskParticle < 0.5) {
        color += vec3(0.0, 1.0, 0.0) * density * 0.02;
        return;
    }

    float noise = 1.0;
    for (int i = 0; i < 12; i++) {
        if (i >= int(pc.adiskNoiseLOD))
            break;
        noise *= 0.5 * snoise(sphericalCoord * pow(float(max(i, 1)), 2.0) * pc.adiskNoiseScale) + 0.5;
        sphericalCoord.y += (i % 2 == 0 ? 1.0 : -1.0) * pc.time * pc.adiskSpeed;
    }

    vec3 dustColor = texture(colorMap, vec2(sphericalCoord.x / outerRadius, 0.5)).rgb;
    color += density * pc.adiskLit * dustColor * alpha * abs(noise);
}

vec3 traceColor(vec3 pos, vec3 dir)
{
    vec3 color = vec3(0.0);
    float alpha = 1.0;
    float stepSize = 0.1;
    dir *= stepSize;

    vec3 h = cross(pos, dir);
    float h2 = dot(h, h);

    for (int i = 0; i < 300; i++) {
        if (pc.renderBlackHole > 0.5) {
            if (pc.gravatationalLensing > 0.5)
                dir += accel(h2, pos);
            if (dot(pos, pos) < 1.0)
                return color;
            if (pc.adiskEnabled > 0.5)
                adiskColor(pos, color, alpha);
        }
        pos += dir;
    }

    dir = rotateVector(dir, vec3(0.0, 1.0, 0.0), pc.time);
    color += texture(galaxy, dir).rgb * alpha;
    return color;
}

void main()
{
    vec3 cameraPos;
    if (pc.mouseControl > 0.5) {
        vec2 mouse = clamp(vec2(pc.mouseX, pc.mouseY) / pc.resolution.xy, 0.0, 1.0) - 0.5;
        cameraPos = vec3(-cos(mouse.x * 10.0) * 15.0, mouse.y * 30.0, sin(mouse.x * 10.0) * 15.0);
    } else if (pc.frontView > 0.5) {
        cameraPos = vec3(10.0, 1.0, 10.0);
    } else if (pc.topView > 0.5) {
        cameraPos = vec3(15.0, 15.0, 0.0);
    } else {
        cameraPos = vec3(-cos(pc.time * 0.1) * 15.0, sin(pc.time * 0.1) * 15.0, sin(pc.time * 0.1) * 15.0);
    }

    mat3 view = lookAt(cameraPos, vec3(0.0), radians(pc.cameraRoll));
    vec2 uv = gl_FragCoord.xy / pc.resolution.xy - vec2(0.5);
    uv.x *= pc.resolution.x / pc.resolution.y;
    vec3 dir = normalize(vec3(-uv.x * pc.fovScale, uv.y * pc.fovScale, 1.0));
    fragColor = vec4(traceColor(cameraPos, view * dir), 1.0);
}
