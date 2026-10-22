#ifndef WATER_H
#define WATER_H

#include "utils.h"
#include "detection.h"
#include "sky.h"
#include "clouds.h"
#include "noise.h"

// fresnel - Schlick's approximation
float calculateFresnel(float cosR, float r0) {
  float a = 1.0-cosR;
  float a2 = a*a;
  return r0 + (1.0-r0)*a2*a2*a;
}

vec4 nlWater(
  inout vec4 color, inout vec3 wPos, nl_skycolor skycol, nl_environment env, vec4 COLOR, vec3 viewDir,
  vec3 cPos, vec3 tiledCpos, vec3 gPos, vec3 CAMERA_POS, vec3 light, vec3 torchColor, vec2 lit,
  float fractCposY, float camDist, highp float t
) {

  // Multi-directional phase-shifted water ripple waves
  vec2 wavePos = gPos.xz * 0.45;
  float wt = t * NL_WATER_WAVE_SPEED;
  vec2 waveNorm;
  waveNorm.x = sin(wavePos.x * 1.5 + wt * 1.4 + sin(wavePos.y * 1.2)) * 0.6
             + cos(wavePos.x * 3.1 - wt * 2.0 + wavePos.y * 1.8) * 0.4;
  waveNorm.y = cos(wavePos.y * 1.5 + wt * 1.3 + cos(wavePos.x * 1.1)) * 0.6
             + sin(wavePos.y * 3.1 - wt * 1.8 + wavePos.x * 1.7) * 0.4;

  vec3 nrm;
  if (fractCposY > 0.0) { // top plane
    nrm.xz = waveNorm * NL_WATER_BUMP;
    nrm.y = -1.0;
  } else { // reflection for side plane
    vec2 bump = 0.5 + 0.5 * sin(vec2(3.0 * wt + cPos.y * PI_HALF, 2.5 * wt + cPos.y * PI_HALF));
    nrm.xz = normalize(viewDir.xz) + bump.y * (1.0 - viewDir.xz * viewDir.xz) * NL_WATER_BUMP;
    nrm.y = bump.x * NL_WATER_BUMP;
  }
  nrm = normalize(nrm);

  float cosR = dot(nrm, viewDir);
  viewDir = viewDir - 2.0 * cosR * nrm; // reflect(viewDir, nrm)

  vec3 waterRefl = nlRenderSky(skycol, env, viewDir, t, false);

  #if defined(NL_CLOUD_AURORA_REFLECTION)
    if (viewDir.y < 0.0) {
      vec4 cloudRefl = nlCloudAuroraReflection(skycol, env, viewDir, wPos, CAMERA_POS, t);
      waterRefl = mix(waterRefl, cloudRefl.rgb, cloudRefl.a);
    }
  #endif

  // BSL Sunlight Specular: Clean sparkle at noon, elongated fiery golden trail at sunset
  if (env.dayFactor > -0.15 && lit.y > 0.70) {
    float dawnFactor = 1.0 - env.dayFactor * env.dayFactor;
    dawnFactor *= dawnFactor;

    vec3 sunV = vec3(env.sunDir.x, -env.sunDir.y, env.sunDir.z);
    float sunDot = max(dot(viewDir, sunV), 0.0);

    // Tight specular reflection core
    float specCore = pow(sunDot, 128.0);
    // Elongated glistening trail across wave ripples along sun azimuth
    float specTrail = pow(sunDot, 16.0) * (0.35 + 0.65 * abs(waveNorm.x));
    float spec = (specCore * 3.5 + specTrail * 1.4) * (1.0 - 0.95 * env.rainFactor);

    // Specular tint: pure warm white in daytime, intense golden amber at sunset
    vec3 sunSpecCol = mix(vec3(1.10, 1.05, 0.95), NL_DAWN_HORIZON_COL * 1.5, dawnFactor);
    waterRefl += sunSpecCol * (spec * 3.8);
  }

  // torch light reflection
  float tc = 0.5 + 0.5 * sin(16.0 * viewDir.x) * sin(16.0 * viewDir.z);
  waterRefl += torchColor * NL_TORCHLIGHT_INTENSITY * lit.x * tc * tc;

  // mask sky reflection under shade
  if (!env.end) {
    waterRefl *= 0.05 + lit.y * 1.14;
  }

  #ifdef NL_WATER_REFL_MASK
    float mask = 0.05 + 0.05 * sin(viewDir.x * 12.0) * sin(viewDir.z * 6.0);
    waterRefl *= smoothstep(mask - 0.2, mask + 0.13, viewDir.y * viewDir.y);
  #endif

  cosR = abs(cosR);
  float fresnel = calculateFresnel(cosR, 0.065);
  float opacity = 1.0 - cosR;

  // BSL Water Surface Color & High Clarity (reveals riverbed while mirroring sky/clouds)
  color.rgb = mix(color.rgb, NL_WATER_TINT * 0.75, 0.82) * (1.0 - 0.65 * fresnel);
  color.a = mix(COLOR.a * BSL_WATER_SURFACE.a, 0.96, opacity * opacity);

  #ifdef NL_WATER_WAVE
    if (camDist < 14.0) {
      wPos.y -= 0.5 * (waveNorm.x + 0.5) * NL_WATER_BUMP;
    }
  #endif

  return vec4(waterRefl, fresnel);
}

#endif
