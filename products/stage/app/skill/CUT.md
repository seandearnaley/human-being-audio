# Stage cuts

A Cut joins two scenes. Stage plays it when a note starts while the last one is still held. It is one WGSL function in one file:

```wgsl
fn cut(uv: vec2f, progress: f32) -> vec4f
```

- `uv` is the pixel's position, 0 to 1, with the origin at the top left.
- `progress` runs 0 to 1 across the cut. At 0 the result must be the outgoing picture and at 1 the incoming one. Stage shows the plain scenes outside that range.
- `scene_from(uv)` samples the outgoing scene and `scene_to(uv)` the incoming one. Both return a premultiplied `vec4f`. Sample them at any `uv`, as often as needed. Outside 0 to 1 the edge pixel repeats.
- `u` carries the music: `u.res` (output size in pixels), `u.beat`, `u.phase` (0 to 1 across the beat), `u.bar` (0 to 1 across the bar), `u.low`, `u.mid`, `u.high` and `u.onset` (0 to 1), and `u.n` (frame index).

## Rules

1. **Only functions and constants.** No bindings, no entry points, no textures of your own. Stage owns those.
2. **Prefix every helper and constant with `cut_`.** The file is joined to Stage's own shader, and a clashing name stops it compiling.
3. **Same inputs, same pixels.** For noise, hash `uv`, `u.n` or `u.beat`.
