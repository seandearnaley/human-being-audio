// The look: one pass over the layered scenes. This is what stops the output reading as a web page.
// `layers(uv)` returns the composited scenes at uv; `prev` is the last output frame (for trails).
// Inputs in `u`: res, beat, phase, bar, low, mid, high, onset, n.

fn look(uv: vec2f) -> vec4f {
  let centre = uv - 0.5;
  let radius = length(centre);

  // Kick: push the picture outward from the centre.
  var p = uv - centre * u.low * 0.035;

  // Snare: tear alternate rows sideways.
  let row = floor(uv.y * 48.0);
  p.x += (fract(sin(row * 91.7 + floor(u.beat)) * 43758.5) - 0.5) * u.mid * 0.03;

  // Any hit: split the colour channels along the radius.
  let split = centre * u.onset * 0.012;
  let r = layers(p + split);
  let g = layers(p);
  let b = layers(p - split);
  var colour = vec4f(r.r, g.g, b.b, max(g.a, max(r.a, b.a)));

  // Trails: the previous frame, zoomed slightly, fading.
  let trail = textureSampleLevel(prev, samp, 0.5 + centre * 0.985, 0.0);
  colour = max(colour, trail * 0.86);

  // Finish: vignette, scanlines, a little grain locked to the frame index.
  let grain = fract(sin(dot(uv * u.res + u.n, vec2f(12.9898, 78.233))) * 43758.5453) - 0.5;
  let lines = 0.94 + 0.06 * sin(uv.y * u.res.y * 3.14159);
  let rgb = colour.rgb * lines * (1.0 - radius * radius * 0.9) + grain * 0.03;
  return vec4f(rgb, 1.0);
}
