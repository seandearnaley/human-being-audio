const cut_SLATS: f32 = 12.0;

fn cut(uv: vec2f, progress: f32) -> vec4f {
  let fy = uv.y * cut_SLATS;
  let i = min(floor(fy), cut_SLATS - 1.0);
  let ly = fy - i - 0.5;
  let start = i / (cut_SLATS - 1.0) * 0.5;
  let p = clamp((progress - start) / 0.5, 0.0, 1.0);
  let ang = p * 1.5707963;
  let h = cos(ang);
  let back = scene_to(uv);
  if (h > 0.001 && abs(ly) < 0.5 * h) {
    let sy = (i + 0.5 + ly / h) / cut_SLATS;
    let c = scene_from(vec2f(uv.x, sy));
    let shade = mix(1.0, 0.45, sin(ang)) * (1.0 - 0.25 * sin(ang) * (ly / max(h, 0.001) + 0.5));
    return vec4f(c.rgb * shade, c.a);
  }
  // shadow cast by the slat onto the revealed scene
  let sh = 1.0 - 0.5 * h * smoothstep(0.5, 0.5 * h, abs(ly));
  return vec4f(back.rgb * sh, back.a);
}
