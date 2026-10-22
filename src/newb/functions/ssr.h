#ifndef SSR_H
#define SSR_H

#if defined(TRANSPARENT) && defined(NL_SSR)

SAMPLER2D_AUTOREG(s_ScreenBuffer);
SAMPLER2D_AUTOREG(s_SceneDepthBuffer);

// Hash and noise for shoreline foam
float foamHash(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float foamNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(
    mix(foamHash(i), foamHash(i + vec2(1.0, 0.0)), f.x),
    mix(foamHash(i + vec2(0.0, 1.0)), foamHash(i + vec2(1.0, 1.0)), f.x),
    f.y
  );
}

// Convert depth buffer value (0..1) to linear view distance in blocks
float sceneDepthLinear(float d) {
  #ifdef SCENE_DEPTH_REVERSED
    d = 1.0 - d;
  #endif
  float ndc = d * 2.0 - 1.0;
  return (2.0 * SCENE_DEPTH_NEAR * SCENE_DEPTH_FAR) / (SCENE_DEPTH_FAR + SCENE_DEPTH_NEAR - ndc * (SCENE_DEPTH_FAR - SCENE_DEPTH_NEAR));
}

void nlApplySSR(
  inout vec4 diffuse,
  vec4 v_refl,
  vec3 wPos,
  vec3 camPos,
  float time,
  sampler2D lightmapTex,
  vec2 lightmapUV,
  float fogAlpha
) {
  vec3 viewDir = normalize(wPos - camPos);
  vec3 faceNormal = normalize(cross(dFdx(wPos), dFdy(wPos)));

  // Water screen-space reflections on horizontal top faces
  if (abs(faceNormal.y) > 0.85 && viewDir.y < 0.0) {
    // Smooth sinusoidal water ripples (BSL & Rinloid style for organic reflection curvature)
    vec2 wavePos = wPos.xz * 0.45;
    float wt = time * 0.7;
    vec2 waveNorm;
    waveNorm.x = sin(wavePos.x * 1.5 + wt * 1.4 + sin(wavePos.y * 1.2)) * 0.6
               + cos(wavePos.x * 3.1 - wt * 2.0 + wavePos.y * 1.8) * 0.4;
    waveNorm.y = cos(wavePos.y * 1.5 + wt * 1.3 + cos(wavePos.x * 1.1)) * 0.6
               + sin(wavePos.y * 3.1 - wt * 1.8 + wavePos.x * 1.7) * 0.4;

    vec2 tilt = waveNorm * NL_SSR_RIPPLE;
    vec3 normal = normalize(vec3(tilt.x, 1.0, tilt.y));
    vec3 refl = reflect(viewDir, normal);
    refl.y = max(refl.y, 0.01);

    // Rinloid Schlick 5th-power Fresnel calculation
    float cosTheta = 1.0 - abs(dot(viewDir, normal));
    float fresnel = clamp(cosTheta * 1.1, 0.0, 1.0);
    float F = fresnel * fresnel * fresnel * fresnel * fresnel; // pow(cosTheta, 5.0)

    // Raymarching in screen space with SceneDepthBuffer
    vec2 hitUV = vec2(0.5, 0.5);
    bool hit = false;

    #ifdef NL_SSR_RAYMARCH
      const int NUM_STEPS = 6;
      float stepLen = NL_SSR_REFLECT_DIST / float(NUM_STEPS);
      for (int i = 1; i <= NUM_STEPS; i++) {
        vec3 p = wPos + refl * (float(i) * stepLen);
        vec4 c = mul(u_viewProj, vec4(p, 1.0));
        if (c.w > 0.0) {
          vec2 suv = c.xy / c.w * 0.5 + 0.5;
          #ifdef SCREEN_BUFFER_FLIP_Y
            suv.y = 1.0 - suv.y;
          #endif
          if (suv.x >= 0.005 && suv.x <= 0.995 && suv.y >= 0.005 && suv.y <= 0.995) {
            float sceneD = sceneDepthLinear(texture2D(s_SceneDepthBuffer, suv).r);
            float rayD = sceneDepthLinear(c.z / c.w);
            if (rayD >= sceneD && (rayD - sceneD) < (stepLen * 1.6)) {
              hitUV = suv;
              hit = true;
              break;
            }
          }
        }
      }
    #endif

    // Fallback: project at maximum reflection distance if raymarch finds no occluder
    if (!hit) {
      vec4 clip = mul(u_viewProj, vec4(wPos + refl * NL_SSR_REFLECT_DIST, 1.0));
      if (clip.w > 0.0) {
        hitUV = clip.xy / clip.w * 0.5 + 0.5;
        #ifdef SCREEN_BUFFER_FLIP_Y
          hitUV.y = 1.0 - hitUV.y;
        #endif
        if (hitUV.x >= 0.0 && hitUV.x <= 1.0 && hitUV.y >= 0.0 && hitUV.y <= 1.0) {
          hit = true;
        }
      }
    }

    // Screen edge vignette fade
    vec2 past = max(-hitUV, hitUV - vec2(1.0, 1.0));
    float over = max(past.x, past.y);
    float edgeFactor = 1.0 - smoothstep(0.0, NL_SSR_EDGE_FADE, max(over, 0.0));
    vec2 m = clamp(hitUV, 0.002, 0.998);

    // Sample ScreenBuffer: razor sharp crystal-clear reflection by default (Rinloid style)
    #ifdef NL_SSR_SOFT_BLUR
      vec2 o = vec2(NL_SSR_BLUR, NL_SSR_BLUR);
      vec3 screenRefl = texture2D(s_ScreenBuffer, m).rgb * 0.4
        + texture2D(s_ScreenBuffer, clamp(m + vec2(o.x, o.y), 0.002, 0.998)).rgb * 0.15
        + texture2D(s_ScreenBuffer, clamp(m + vec2(-o.x, o.y), 0.002, 0.998)).rgb * 0.15
        + texture2D(s_ScreenBuffer, clamp(m + vec2(o.x, -o.y), 0.002, 0.998)).rgb * 0.15
        + texture2D(s_ScreenBuffer, clamp(m + vec2(-o.x, -o.y), 0.002, 0.998)).rgb * 0.15;
    #else
      vec3 screenRefl = texture2D(s_ScreenBuffer, m).rgb;
    #endif

    // Subtle water absorption tint
    screenRefl *= mix(vec3_splat(1.0), clamp(diffuse.rgb * 1.8, 0.0, 1.0) * 0.6 + 0.4, NL_SSR_TINT);

    // Rinloid Fallback: seamlessly transition to BSL sky + cumulus clouds where screen ends
    vec3 skyFallback = v_refl.rgb;
    vec3 finalRefl = mix(skyFallback, screenRefl, edgeFactor * (hit ? 1.0 : 0.0));

    // Blend reflection into water color using Rinloid Fresnel
    float reflStrength = NL_SSR_STRENGTH * F * (1.0 - fogAlpha);
    diffuse.rgb = mix(diffuse.rgb, finalRefl, clamp(reflStrength, 0.0, 0.95));

    // Preserve sun & moon specular glint from v_refl
    diffuse.rgb += v_refl.rgb * (v_refl.a * 0.5);

    // Rinloid Water Opacity Boost: increases alpha at grazing angles to eliminate riverbed ghosting
    diffuse.a = mix(diffuse.a, 0.95, clamp(F * 0.90, 0.0, 0.95));
  } else {
    // Non-horizontal faces fallback to standard reflection
    diffuse.rgb += v_refl.rgb * v_refl.a;
  }

  #ifdef NL_SSR_FOAM
    // Water shoreline foam using scene depth buffer
    #if BGFX_SHADER_LANGUAGE_GLSL >= 300 || BGFX_SHADER_LANGUAGE_HLSL >= 400 || defined(BGFX_SHADER_LANGUAGE_METAL)
      vec2 depthUV = gl_FragCoord.xy / vec2(textureSize(s_SceneDepthBuffer, 0));
    #else
      vec2 depthUV = gl_FragCoord.xy / u_viewRect.zw;
    #endif

    float behind = sceneDepthLinear(texture2D(s_SceneDepthBuffer, depthUV).r);
    float thickness = max(behind - sceneDepthLinear(gl_FragCoord.z), 0.0);

    float t = time * NL_SSR_FOAM_SPEED;
    vec2 wp = wPos.xz * NL_SSR_FOAM_SCALE;
    float n = foamNoise(wp + vec2(t, t * 0.7)) * 0.6 + foamNoise(wp * 2.1 - vec2(t * 0.8, t)) * 0.4;
    float shore = 1.0 - clamp(thickness / NL_SSR_FOAM_WIDTH, 0.0, 1.0);
    shore *= 0.8 + 0.2 * sin(t * 3.0 + n * 6.28);
    float foam = smoothstep(0.45, 0.65, n + shore * 0.6 - 0.3) * smoothstep(0.0, 0.15, shore);
    foam *= 1.0 - fogAlpha;

    diffuse.rgb = mix(diffuse.rgb, vec3(0.92, 0.96, 1.0) * texture2D(lightmapTex, lightmapUV).rgb, foam);
    diffuse.a = max(diffuse.a, foam);
  #endif
}

#endif // TRANSPARENT && NL_SSR

#endif // SSR_H
