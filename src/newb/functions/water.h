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

  vec2 bump = vec2_splat(movingNoise2D(gPos.xz + gPos.yy, NL_WATER_WAVE_SPEED*t, 0.6));

  vec3 nrm;
  if (fractCposY > 0.0) { // top plane
    nrm.xz = bump*NL_WATER_BUMP;
    nrm.y = -1.0;
    /*if (fractCposY>0.8 || fractCposY<0.9) { // flat plane
    } else { // slanted plane and highly slanted plane
    }*/
  } else { // reflection for side plane
    bump *= 0.5 + 0.5*sin(3.0*t*NL_WATER_WAVE_SPEED + cPos.y*PI_HALF);
    nrm.xz = normalize(viewDir.xz) + bump.y*(1.0-viewDir.xz*viewDir.xz)*NL_WATER_BUMP;
    nrm.y = bump.x*NL_WATER_BUMP;
  }
  nrm = normalize(nrm);

  float cosR = dot(nrm, viewDir);
  viewDir = viewDir - 2.0*cosR*nrm ; // reflect(viewDir, nrm)

  vec3 waterRefl = nlRenderSky(skycol, env, viewDir, t, false);

  #if defined(NL_CLOUD_AURORA_REFLECTION)
    if (viewDir.y < 0.0) {
      vec4 cloudRefl = nlCloudAuroraReflection(skycol, env, viewDir, wPos, CAMERA_POS, t);
      waterRefl = mix(waterRefl, cloudRefl.rgb, cloudRefl.a);
    }
  #endif

  // BSL Sunlight Specular: Clean, soft white specular highlight
  if (env.dayFactor > -0.1 && lit.y > 0.75) {
    vec3 sunV = vec3(env.sunDir.x, -env.sunDir.y, env.sunDir.z);
    float sunDot = max(dot(viewDir, sunV), 0.0);
    float specHighlight = pow(sunDot, 64.0) * (1.0 - 0.9 * env.rainFactor);
    waterRefl += vec3(1.0, 0.98, 0.95) * (specHighlight * 3.5);
  }

  // torch light reflection
  float tc = 0.5+0.5*sin(16.0*viewDir.x)*sin(16.0*viewDir.z);
  waterRefl += torchColor*NL_TORCHLIGHT_INTENSITY*lit.x*tc*tc;

  // mask sky reflection under shade
  if (!env.end) {
    waterRefl *= 0.05 + lit.y*1.14;
  }

  #ifdef NL_WATER_REFL_MASK
    float mask = 0.05+0.05*sin(viewDir.x*12.0)*sin(viewDir.z*6.0);
    waterRefl *= smoothstep(mask-0.2,mask+0.13,viewDir.y*viewDir.y);
  #endif

  cosR = abs(cosR);
  float fresnel = calculateFresnel(cosR, 0.08);
  float opacity = 1.0-cosR;

  // BSL Water Surface Color & Clarity (~65% transparency in shallow water)
  color.rgb = mix(color.rgb, NL_WATER_TINT * 0.75, 0.85) * (1.0 - 0.7 * fresnel);
  color.a = mix(COLOR.a * BSL_WATER_SURFACE.a, 1.0, opacity*opacity);

  #ifdef NL_WATER_WAVE
    if (camDist < 14.0) {
      wPos.y -= 0.5*(bump.x+0.5)*NL_WATER_BUMP;
    }
  #endif

  return vec4(waterRefl, fresnel);
}

#endif
