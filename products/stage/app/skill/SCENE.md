---
name: stage-scenes
description: Write tempo-locked visual scenes for Stage. A scene is one HTML file that Stage drives with musical time. Use when asked to create or edit a Stage scene.
---

# Stage scenes

A scene is one self-contained HTML file. Stage loads it in a frame, feeds it musical time once per video frame, and layers it with other scenes over black. The same file plays live beside a DAW and renders offline to video, so the picture must depend only on what Stage gives you.

A scene never knows the song. It is cut in and out at any point, over any drum pattern, at any tempo. **React to the hits Stage reports. Never work out where a drum "should" be from the beat.**

## What Stage gives you, in this order, every frame

**1. CSS custom properties on `<html>`.** Unitless numbers that inherit. Use them in `calc()`.

| Property | Value |
| --- | --- |
| `--kick` `--snare` `--hat` | One envelope per drum. Jumps to 1 when that drum hits, then decays to about 0.05 half a beat later. **Use these for anything that should hit.** |
| `--onset` | The same shape for any hit at all. |
| `--low` `--mid` `--high` | 0 to 1 loudness of bass, mids and highs. Slower and smoother than the drum envelopes. A bassline moves `--low` too. |
| `--level` | 0 to 1 overall loudness. |
| `--beat` | Song position in beats, fractional. Beat 0 is a bar line. It does not restart when your scene is cut in. |
| `--phase` | 0 to 1 across the current beat, then it jumps back to 0. |
| `--bar` | 0 to 1 across the current bar, then it jumps back to 0. Follows the time signature. |
| `--bpm` | Tempo. |
| `--age` | Beats since this scene was cut in. Use it for intros. |
| `--t` | Seconds since this scene was cut in. |
| `--<name>` | One per parameter you declare. |

**2. A `frame` event on `window`.** `event.detail` holds the same numbers without the dashes (`kick`, `snare`, `hat`, `onset`, `low`, `mid`, `high`, `level`, `beat`, `phase`, `bar`, `bpm`, `age`, `t`) plus what CSS cannot carry:

| Field | Value |
| --- | --- |
| `n`, `fps`, `dt` | Frame index from the cut, frame rate, seconds per frame. |
| `beatIndex`, `barIndex`, `sig` | Whole beat in the bar (0-based), whole bar (0-based), time signature such as `[4, 4]`. |
| `width`, `height` | Output size in device pixels. Set `canvas.width` and `canvas.height` to these and stretch the canvas to the viewport with CSS. |
| `seed` | An integer. The only allowed source of randomness. |
| `bands` | `Float32Array(8)`, 0 to 1, low to high. |
| `spectrum` | `Float32Array(64)`, 0 to 1, low to high. |
| `notes` | Recent MIDI notes: `[{ note, velocity, channel, age }]`. `velocity` is 0 to 1, `age` is beats since note-on. A note stays listed for one beat after it starts. Kick is 36, snare 38, closed hat 42. |
| `params` | `{ name: value }` for the parameters you declared. |

**3. The animation clock.** Stage pauses every CSS animation and Web Animation, including animations of registered custom properties, and sets its time to `beat × 1s`. So write durations in beats. `animation: spin 4s linear infinite` turns once per bar in 4/4 at any tempo and stays on the bar line, because time 0 is a bar line. A negative `animation-delay` shifts the phase: `-1s` starts one beat in. For other time signatures use `--bar`.

Use the clock for motion that belongs to the grid: sweeps, rotations, counters. Use the drum envelopes for hits.

## The manifest

One JSON block in the file. `scene` is the contract version, currently 1. Each parameter becomes a knob, a `--name` property and a `params.name` value.

```html
<script type="application/json" id="scene">
{ "scene": 1, "name": "Pulse Ring", "backdrop": false,
  "params": [
    { "name": "hue", "type": "number", "default": 200, "min": 0, "max": 360 },
    { "name": "mirror", "type": "boolean", "default": 0 }
  ] }
</script>
```

Parameter types are `number` (with `min` and `max`) and `boolean` (0 or 1). Set `backdrop` to `true` only if the scene paints every pixel. Otherwise leave the page background transparent so scenes layer.

## Rules

1. **React, never predict.** Hits come from `--kick`, `--snare`, `--hat`, `--onset` or `notes`. Do not compute hit times from `beat`, `phase`, `bar` or keyframe percentages. Stage tests every scene by moving the drums.
2. **Same inputs, same pixels.** Never use `Date`, `performance.now`, `requestAnimationFrame`, `setTimeout`, `setInterval`, `Math.random` or CSS `transition`. Draw only inside the `frame` handler. For randomness, seed your own generator from `detail.seed`.
3. **One file, nothing external.** No network, CDN, imports, web fonts or image URLs. Use system font stacks, inline SVG and data URIs.
4. **Small DOM, dense canvas.** Keep the page under about 200 elements. Draw dense visuals on a `<canvas>` (2D, WebGL or WebGPU).
5. **Frames arrive in order** from the cut, `dt` apart. Prefer computing the picture from the current frame's values alone. If you keep state (particles, trails), advance it only in the `frame` handler, using `dt` and your seeded generator.
6. **Async setup** such as a WebGPU device: assign the promise to `window.ready`. Stage waits for it before the first frame.
7. **Fill the viewport.** Use `vw`, `vh`, `vmin`. Any aspect ratio may be used.

## Example

```html
<!doctype html>
<meta charset="utf-8">
<title>Pulse Ring</title>
<script type="application/json" id="scene">
{ "scene": 1, "name": "Pulse Ring", "backdrop": false,
  "params": [ { "name": "hue", "type": "number", "default": 200, "min": 0, "max": 360 } ] }
</script>
<style>
  html, body { margin: 0; height: 100%; overflow: hidden; }
  body { display: grid; place-items: center; }
  canvas { position: fixed; inset: 0; width: 100%; height: 100%; }
  .ring {
    width: 40vmin; aspect-ratio: 1; border-radius: 50%;
    border: calc(1vmin + var(--snare) * 3vmin) solid hsl(calc(var(--hue) * 1deg) 90% 60%);  /* snare thickens it */
    scale: calc(1 + var(--kick) * 0.5);       /* kick swells it */
    rotate: calc(var(--bar) * 1turn);         /* grid: one turn per bar */
    opacity: calc(0.4 + var(--hat) * 0.6);    /* hats flicker it */
  }
</style>
<canvas id="c"></canvas>
<div class="ring"></div>
<script>
  const c = document.getElementById('c'), g = c.getContext('2d');
  addEventListener('frame', ({ detail: f }) => {
    if (c.width !== f.width) { c.width = f.width; c.height = f.height; }
    g.clearRect(0, 0, c.width, c.height);
    g.fillStyle = `hsl(${f.params.hue} 90% 60% / .5)`;
    const w = c.width / f.spectrum.length;
    f.spectrum.forEach((v, i) => g.fillRect(i * w, c.height, w * 0.8, -v * c.height * 0.5));
  });
</script>
```

The CSS half needs no JavaScript. A scene with no `frame` listener is valid. The example is deliberately plain: do not copy its ring.

## Before you finish

- Would the scene still hit correctly if the kick and snare swapped places, or the pattern were four-on-the-floor? If not, rule 1 is broken.
- Search the file for every word in rule 2.
- Check that nothing loads from a URL.
- Check that each `animation` duration is a whole or simple fraction of beats.
