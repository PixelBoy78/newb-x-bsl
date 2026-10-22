#ifndef FOG_H
#define FOG_H

float nlRenderFogFade(float relativeDist, vec3 FOG_COLOR, vec2 FOG_CONTROL) {
  #ifdef NL_FOG
    // Detect rain from FOG_CONTROL: clear weather y=1.0, rain y=0.70
    float rain = clamp((0.96 - FOG_CONTROL.y) / 0.25, 0.0, 1.0);

    // BSL Atmospheric Exponential Fog
    // On clear days: soft gradual fade into distant sky (Image 1 & 4)
    // On rainy days: dense moody slate-teal fog blanketing the scene (Image 2)
    float fogStart = FOG_CONTROL.x * mix(0.55, 0.20, rain);
    float fogEnd = mix(FOG_CONTROL.y, FOG_CONTROL.y * 0.75, rain);
    float d = clamp((relativeDist - fogStart) / max(fogEnd - fogStart, 0.001), 0.0, 1.0);
    float fade = 1.0 - exp(-d * d * mix(3.0, 5.5, rain));
    fade = smoothstep(0.0, 1.0, fade);

    // Atmospheric mist depth / low humidity haze
    float density = NL_MIST_DENSITY * mix(7.0, 22.0, rain);
    fade += (1.0 - fade) * mix(0.25, 0.60, rain) * (1.0 - exp(-relativeDist * relativeDist * density));

    return clamp(NL_FOG * fade, 0.0, 1.0);
  #else
    return 0.0;
  #endif
}

float nlRenderGodRayIntensity(vec3 cPos, vec3 worldPos, float t, vec2 uv1, float relativeDist, vec3 FOG_COLOR) {
  // offset wPos (only works upto 16 blocks)
  vec3 offset = cPos - 16.0*fract(worldPos*0.0625);
  offset = abs(2.0*fract(offset*0.0625)-1.0);
  offset = offset*offset*(3.0-2.0*offset);
  //offset = 0.5 + 0.5*cos(offset*0.392699082);

  //vec3 ofPos = wPos+offset;
  vec3 nrmof = normalize(worldPos);

  float u = nrmof.z/length(nrmof.zy);
  float diff = dot(offset,vec3(0.1,0.2,1.0)) + 0.07*t;
  float mask = nrmof.x*nrmof.x;

  float vol = sin(7.0*u + 1.5*diff)*sin(3.0*u + diff);
  vol *= vol*mask*uv1.y*(1.0-mask*mask);
  vol *= relativeDist*relativeDist;

  // dawn/dusk mask
  vol *= clamp(3.0*(FOG_COLOR.r-FOG_COLOR.b), 0.0, 1.0);

  vol = smoothstep(0.0, 0.1, vol);
  return vol;
}

#endif
