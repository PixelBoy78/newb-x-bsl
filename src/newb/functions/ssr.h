#ifndef SSR_H
#define SSR_H

#if defined(TRANSPARENT) && defined(NL_SSR)

SAMPLER2D_AUTOREG(s_ScreenBuffer);
SAMPLER2D_AUTOREG(s_SceneDepthBuffer);

// Noise functions for water ripples and shoreline foam
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

// Convert depth buffer value (0..1) to linear view distance (blocks)
float sceneDepthLinear(float d) {
  #ifdef SCENE_DEPTH_REVERSED
    d = 1.0 - d;
  #endif
  float ndc = d * 2.0 - 1.0;
  return (2.0 * SCENE_DEPTH_NEAR * SCENE_DEPTH_FAR) / (SCENE_DEPTH_FAR + SCENE_DEPTH_NEAR - ndc * (SCENE_DEPTH_FAR - SCENE_DEPTH_NEAR));
}

void nlApplySSR(
  inout vec4 diffuse,
  vec3 wPos,
  vec3 camPos,
  float time,
  sampler2D lightmapTex,
  vec2 lightmapUV,
  float fogAlpha
) {
  // Screen-space water reflections on top faces
  vec3 viewDir = normalize(wPos - camPos);
  vec3 faceNormal = normalize(cross(dFdx(wPos), dFdy(wPos)));

  if (abs(faceNormal.y) > 0.85 && viewDir.y < 0.0) {
    vec2 rp = wPos.xz * 1.7;
    vec2 rippleA = vec2(foamNoise(rp + vec2(time * 0.4, time * 0.3)), foamNoise(rp - vec2(time * 0.3, time * 0.5) + 7.3)) - 0.5;
    vec2 rippleB = vec2(foamNoise(rp * 2.9 - vec2(time * 0.7, time * 0.2) + 3.1), foamNoise(rp * 2.9 + vec2(time * 0.2, time * 0.6) + 11.7)) - 0.5;
    vec2 tilt = (rippleA + rippleB * 0.5) * NL_SSR_RIPPLE;
    vec3 normal = normalize(vec3(tilt.x, 1.0, tilt.y));
    vec3 refl = reflect(viewDir, normal);
    refl.y = max(refl.y, 0.02);

    // Project reflected ray into screen space
    vec4 clip = mul(u_viewProj, vec4(wPos + refl * NL_SSR_REFLECT_DIST, 1.0));
    if (clip.w > 0.0) {
      vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
      #ifdef SCREEN_BUFFER_FLIP_Y
        uv.y = 1.0 - uv.y;
      #endif

      // Edge fade and mirrored boundary handling
      vec2 past = max(-uv, uv - 1.0);
      float over = max(past.x, past.y);
      vec2 m = clamp(1.0 - abs(1.0 - abs(uv)), 0.002, 0.998);
      float k = NL_SSR_STRENGTH * (1.0 - smoothstep(0.0, NL_SSR_EDGE_FADE, max(over, 0.0)));
      #ifdef NL_SSR_FRESNEL
        k *= mix(0.25, 1.0, pow(1.0 - clamp(-viewDir.y, 0.0, 1.0), 3.0));
      #endif
      k *= 1.0 - fogAlpha;

      // 5-tap blur sampling for smooth reflections
      vec2 o = vec2(NL_SSR_BLUR, NL_SSR_BLUR) + tilt * 0.05;
      vec3 reflected = texture2D(s_ScreenBuffer, m).rgb * 0.4
        + texture2D(s_ScreenBuffer, clamp(m + vec2(o.x, o.y), 0.002, 0.998)).rgb * 0.15
        + texture2D(s_ScreenBuffer, clamp(m + vec2(-o.x, o.y), 0.002, 0.998)).rgb * 0.15
        + texture2D(s_ScreenBuffer, clamp(m + vec2(o.x, -o.y), 0.002, 0.998)).rgb * 0.15
        + texture2D(s_ScreenBuffer, clamp(m + vec2(-o.x, -o.y), 0.002, 0.998)).rgb * 0.15;

      reflected *= mix(vec3_splat(1.0), clamp(diffuse.rgb * 2.0, 0.0, 1.0) * 0.7 + 0.3, NL_SSR_TINT);
      diffuse.rgb = mix(diffuse.rgb, reflected, k);
    }
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
