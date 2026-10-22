#ifndef CLOUDS_H
#define CLOUDS_H

#include "detection.h"
#include "noise.h"
#include "sky.h"

// simple clouds 2D noise
float cloudNoise2D(vec2 p, highp float t, float rain) {
  t *= NL_CLOUD1_SPEED;
  p += t;
  p.y += 3.0*sin(0.3*p.x + 0.1*t);

  vec2 p0 = floor(p);
  vec2 u = p-p0;
  u *= u*(3.0-2.0*u);
  vec2 v = 1.0-u;

  float n = mix(
    mix(rand(p0),rand(p0+vec2(1.0,0.0)), u.x),
    mix(rand(p0+vec2(0.0,1.0)),rand(p0+vec2(1.0,1.0)), u.x),
    u.y
  );
  n *= 0.5 + 0.5*sin(p.x*0.6 - 0.5*t)*sin(p.y*0.6 + 0.8*t);
  n = min(n*(1.0+rain), 1.0);
  return n*n;
}

// simple clouds (BSL Stylized Dual-Tone Shading)
vec4 renderCloudsSimple(nl_skycolor skycol, vec3 pos, highp float t, float rain) {
  pos.xz *= NL_CLOUD1_SCALE;
  float d = cloudNoise2D(pos.xz, t, rain);

  vec3 bottomCol = mix(vec3(0.46, 0.52, 0.62), skycol.zenith * 0.85, 0.35);
  vec3 topCol = mix(skycol.horizonEdge * 0.7, vec3(1.15, 1.08, 0.96), 0.65);
  float topShade = smoothstep(0.55, 0.18, d);
  vec3 cloudColor = mix(bottomCol, topCol, topShade);

  vec4 col = vec4(cloudColor, smoothstep(0.1, 0.6, d));
  col.rgb *= 1.0 - 0.75 * rain;
  return col;
}

// Fast 2D value noise for cumulus coverage
float bslNoise2D(vec2 p) {
  vec2 p0 = floor(p);
  vec2 u = p - p0;
  u = u * u * (3.0 - 2.0 * u);

  float n = mix(
    mix(fastRand(p0), fastRand(p0 + vec2(1.0, 0.0)), u.x),
    mix(fastRand(p0 + vec2(0.0, 1.0)), fastRand(p0 + vec2(1.0, 1.0)), u.x),
    u.y
  );
  return n;
}

// BSL Cumulus 3D Density Map (Capt Tatsu Puffy Billow Aesthetic)
// Organic puffy cumulus lobes with flat base and billowy domes
float cloudDf(vec3 pos, float rain, vec2 boxiness) {
  vec2 p = pos.xz;

  // Macro-structure coverage (creates cloud clumps vs open blue sky)
  float cov1 = bslNoise2D(p * 0.7);
  float cov2 = bslNoise2D(p * 1.5 + vec2(4.3, 8.1));
  float coverage = cov1 * 0.65 + cov2 * 0.35;

  // Worley / Voronoi cellular billows (creates puffy cumulus lobes)
  float v1 = fastVoronoi2(p * 1.25, 1.8);
  float v2 = fastVoronoi2(p * 2.7 + vec2(1.7, 3.1), 1.4);
  float billows = max(v1 * 0.65 + v2 * 0.35, 0.0);

  // Combine coverage and puffy lobes
  float shape = coverage * 0.50 + billows * 0.50;

  // Real cumulus vertical profile: flat bottom base, rounded billowing dome
  float base = smoothstep(0.02, 0.22, pos.y);
  float dome = smoothstep(0.98, 0.38 + 0.30 * shape, pos.y);
  float heightProfile = base * dome;

  // Rain: merges clouds into a dense stormy overcast deck
  float threshold = mix(0.38, 0.12, rain);
  float density = smoothstep(threshold, threshold + 0.32, shape) * heightProfile;

  return density;
}

vec4 renderCloudsRounded(
    vec3 vDir, vec3 vPos, float rain, float time, vec3 horizonCol, vec3 zenithCol, float dayFactor,
    const int steps, const float thickness, const float thickness_rain, const float speed,
    const vec2 scale, const float density, const vec2 boxiness
) {
  float height = 7.0*mix(thickness, thickness_rain, rain);
  float stepsf = float(steps);

  // scaled ray offset
  vec3 deltaP;
  deltaP.y = 1.0;
  deltaP.xz = height*scale*vDir.xz/(0.02+0.98*abs(vDir.y));

  // local cloud pos
  vec3 pos;
  pos.y = 0.0;
  pos.xz = scale*(vPos.xz + vec2(1.0,0.5)*(time*speed));
  pos += deltaP;

  deltaP /= -stepsf;

  // alpha, gradient
  vec2 d = vec2(0.0,1.0);
  for (int i=1; i<=steps; i++) {
    float m = cloudDf(pos, rain, boxiness);
    d.x += m;
    d.y = mix(d.y, pos.y, m);
    pos += deltaP;
  }
  d.x *= smoothstep(0.03, 0.1, d.x);
  d.x /= (stepsf/density) + d.x;

  if (vPos.y < 0.0) { // view from top
    d.y = 1.0 - d.y;
  }

  // BSL Sunlit Cloud Shading:
  float dawnFactor = 1.0 - dayFactor * dayFactor;
  dawnFactor *= dawnFactor;

  // Day: sunlit cream-white top; Sunset: brilliant golden-amber top
  vec3 sunCloudTop = mix(vec3(1.22, 1.18, 1.10), horizonCol * 1.35, dawnFactor);
  // Base underside: slate-blue ambient shadow; Sunset: warm mauve-slate
  vec3 ambientCloudBottom = mix(vec3(0.50, 0.58, 0.70), zenithCol * 0.9, 0.45);
  ambientCloudBottom = mix(ambientCloudBottom, vec3(0.35, 0.28, 0.42), dawnFactor);

  // Rain: dark stormy slate-teal overcast
  sunCloudTop = mix(sunCloudTop, vec3(0.42, 0.48, 0.52), rain);
  ambientCloudBottom = mix(ambientCloudBottom, vec3(0.28, 0.34, 0.38), rain);

  // Vertical light penetration: top is illuminated, bottom in shadow
  float lightGrad = smoothstep(0.15, 0.85, d.y);
  vec3 cloudColor = mix(ambientCloudBottom, sunCloudTop, lightGrad);

  vec4 col = vec4(cloudColor, d.x);
  col.rgb *= 1.0 - 0.75 * rain;
  return col;
}

float cloudsNoiseVr(vec2 p, float t) {
  float n = fastVoronoi2(p + t, 1.8);
  n *= fastVoronoi2(3.0*p + t, 1.5);
  n *= fastVoronoi2(9.0*p + t, 0.4);
  n *= fastVoronoi2(27.0*p + t, 0.1);
  //n *= fastVoronoi2(82.0*pos + t, 0.02); // more quality
  return n*n;
}

vec4 renderClouds(vec2 p, float t, float rain, vec3 horizonCol, vec3 zenithCol, const vec2 scale, const float velocity, const float shadow) {
  p *= scale;
  t *= velocity;

  // layer 1
  float a = cloudsNoiseVr(p, t);
  float b = cloudsNoiseVr(p + NL_CLOUD3_SHADOW_OFFSET*scale, t);

  // layer 2
  p = 1.4 * p.yx + vec2(7.8, 9.2);
  t *= 0.5;
  float c = cloudsNoiseVr(p, t);
  float d = cloudsNoiseVr(p + NL_CLOUD3_SHADOW_OFFSET*scale, t);

  // higher = less clouds thickness
  // lower separation betwen x & y = sharper
  vec2 tr = vec2(0.6, 0.7) - 0.12*rain;
  a = smoothstep(tr.x, tr.y, a);
  c = smoothstep(tr.x, tr.y, c);

  // shadow
  b *= smoothstep(0.2, 0.8, b);
  d *= smoothstep(0.2, 0.8, d);

  vec4 col;
  col.a = a + c*(1.0-a);
  col.rgb = horizonCol + horizonCol.ggg;
  col.rgb = mix(col.rgb, 0.5*(zenithCol + zenithCol.ggg), shadow*mix(b, d, c));
  col.rgb *= 1.0-0.7*rain;

  return col;
}

// aurora is rendered on clouds layer
#ifdef NL_AURORA
vec4 renderAurora(vec3 p, float t, float rain, float dayFactor) {
  t *= NL_AURORA_VELOCITY;
  p.xz *= NL_AURORA_SCALE;
  p.xz += 0.05*sin(p.x*4.0 + 20.0*t);

  float d0 = sin(p.x*0.1 + t + sin(p.z*0.2));
  float d1 = sin(p.z*0.1 - t + sin(p.x*0.2));
  float d2 = sin(p.z*0.1 + 1.0*sin(d0 + d1*2.0) + d1*2.0 + d0*1.0);
  d0 *= d0; d1 *= d1; d2 *= d2;
  d2 = d0/(1.0 + d2/NL_AURORA_WIDTH);

  float mask = max(-dayFactor, 0.0);
  mask *= mask*(1.0-0.8*rain);
  return vec4(NL_AURORA*mix(NL_AURORA_COL1,NL_AURORA_COL2,d1),1.0)*d2*mask;
}
#endif

// BSL Cloud reflection on water surface
vec4 renderCloudsReflection(nl_skycolor skycol, vec2 cloudPos, float rain, float t, float dayFactor) {
  vec2 p = NL_CLOUD2_SCALE * (cloudPos + vec2(1.0, 0.5) * (t * NL_CLOUD2_VELOCITY));
  float d = cloudDf(vec3(p.x, 0.5, p.y), rain, NL_CLOUD2_SHAPE);
  if (d <= 0.01) {
    return vec4(0.0, 0.0, 0.0, 0.0);
  }

  float dawnFactor = 1.0 - dayFactor * dayFactor;
  dawnFactor *= dawnFactor;

  vec3 sunCloudTop = mix(vec3(1.18, 1.14, 1.06), skycol.horizonEdge * 1.30, dawnFactor);
  vec3 ambientCloudBottom = mix(vec3(0.50, 0.58, 0.70), skycol.zenith * 0.9, 0.45);
  sunCloudTop = mix(sunCloudTop, vec3(0.42, 0.48, 0.52), rain);
  ambientCloudBottom = mix(ambientCloudBottom, vec3(0.28, 0.34, 0.38), rain);

  vec3 col = mix(ambientCloudBottom, sunCloudTop, 0.65);
  return vec4(col, clamp(d * 1.6, 0.0, 0.92));
}

vec4 nlCloudAuroraReflection(nl_skycolor skycol, nl_environment env, vec3 viewDir, vec3 wPos, vec3 CAMERA_POS, highp float t) {
  vec2 cloudPos = wPos.xz;
  cloudPos += (187.0-(wPos.y+CAMERA_POS.y))*viewDir.xz/viewDir.y;
  float fade = clamp(2.0 - 0.005*length(cloudPos), 0.0, 1.0);
  cloudPos += CAMERA_POS.xz;

  vec4 refl = vec4_splat(0.0);

  #ifdef NL_AURORA
    vec4 aurora = renderAurora(cloudPos.xyy, t, env.rainFactor, smoothstep(0.2, -0.2, env.dayFactor));
    aurora.a *= fade;
    refl = vec4(2.0*aurora.rgb*aurora.a, aurora.a);
  #endif

  #if NL_CLOUD_TYPE == 2
    vec4 clouds = renderCloudsReflection(skycol, cloudPos, env.rainFactor, t, env.dayFactor);
    clouds.a *= fade;
    refl = vec4(mix(refl.rgb, clouds.rgb, clouds.a), min(refl.a + clouds.a, 1.0));
  #elif NL_CLOUD_TYPE == 1
    vec4 clouds = renderCloudsSimple(skycol, cloudPos.xyy, t, env.rainFactor);
    clouds.a *= fade;
    refl = vec4(mix(refl.rgb, clouds.rgb, clouds.a), min(refl.a + clouds.a, 1.0));
  #endif

  return refl;
}

#endif