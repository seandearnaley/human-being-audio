fn cut_hash(p: vec2f) -> f32 {
  return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453);
}

fn cut_pick(uv: vec2f, to: bool) -> vec4f {
  if (to) { return scene_to(uv); }
  return scene_from(uv);
}

fn cut(uv: vec2f, progress: f32) -> vec4f {
  let p = clamp(progress, 0.0, 1.0);
  let amt = sin(p * 3.14159265);
  let q = floor(p * 14.0);
  let rows = mix(6.0, 22.0, cut_hash(vec2f(q, 1.7)));
  let row = floor(uv.y * rows);
  let col = floor(uv.x * 3.0 + cut_hash(vec2f(row, q + 5.0)) * 2.0);
  let h = cut_hash(vec2f(row, q));
  let jumps = step(0.35, cut_hash(vec2f(row + 7.0, q + 3.0)));
  let shift = (h - 0.5) * 0.45 * amt * jumps;
  let split = (0.015 + 0.03 * u.onset) * amt * (0.5 + jumps);
  let suv = vec2f(uv.x + shift, uv.y);
  let showTo = cut_hash(vec2f(row * 3.1 + col, q + 11.0)) < smoothstep(0.45, 0.88, p);
  let r = cut_pick(suv + vec2f(split, 0.0), showTo);
  let g = cut_pick(suv, showTo);
  let b = cut_pick(suv - vec2f(split, 0.0), showTo);
  return vec4f(r.r, g.g, b.b, max(max(r.a, g.a), b.a));
}
