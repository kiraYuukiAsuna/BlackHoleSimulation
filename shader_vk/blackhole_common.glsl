layout(push_constant) uniform PushConstants {
    vec2 resolution;
    float time;
    float mouseX;
    float mouseY;
    float frontView;
    float topView;
    float cameraRoll;
    float gravatationalLensing;
    float renderBlackHole;
    float mouseControl;
    float fovScale;
    float adiskEnabled;
    float adiskParticle;
    float adiskHeight;
    float adiskLit;
    float adiskDensityV;
    float adiskDensityH;
    float adiskNoiseScale;
    float adiskNoiseLOD;
    float adiskSpeed;
    float bloomStrength;
    float tonemappingEnabled;
    float gamma;
} pc;

