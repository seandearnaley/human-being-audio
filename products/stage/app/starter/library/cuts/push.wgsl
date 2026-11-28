// The Cut: two pictures in, one out. `scene_from(uv)` is the outgoing scene, `scene_to(uv)` the
// incoming one, and `progress` runs 0 to 1 across the cut. The host skips this at 0 and 1.
// Inputs in `u` are the same as the look's: res, beat, phase, bar, low, mid, high, onset, n.

fn cut(uv: vec2f, progress: f32) -> vec4f {
  // A diagonal wipe whose edge shoves the outgoing picture ahead of it.
  let edge = (uv.x + uv.y * 0.35) / 1.35;
  let front = smoothstep(progress - 0.08, progress + 0.08, edge);
  let push = vec2f(front * progress * 0.06, 0.0);
  return mix(scene_to(uv), scene_from(uv + push), front);
}
