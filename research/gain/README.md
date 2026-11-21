# Studio gain study

Livecut's Rush procedure and resonator use the same double-precision amplitude conversion as Tract Form's gain stages. The shared implementation is in [modules/dsp](../../modules/dsp). Its contract is unity at 0 dB, half amplitude at −6.020599913279624 dB, and nonzero gain at −120 dB. The Rush procedure casts to float at its original boundary. The synth retains its Clang real-time checks.

The offline study renders three seconds of stereo audio from each engine at 44.1 and 48 kHz, with blocks of 64 and 257 frames. Livecut processes a generated drum loop through the Rush pad. Tract Form plays MIDI note 29 with a −3 dB mid gain. The inputs, transport and seed are fixed. Generated audio stays in the build directory.

To compare a prospective change with a reference checkout, use the same compiler, architecture and optimization mode for both. Initialize Livecut’s submodules in both checkouts. From the candidate repository run the following for each checkout, changing the two paths.

```sh
cmake -S research/gain -B /tmp/gain-reference -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSTUDIO_SOURCE=/absolute/path/to/reference
cmake --build /tmp/gain-reference --target render-gain
/tmp/gain-reference/render-gain /tmp/gain-reference/pcm

cmake -S research/gain -B /tmp/gain-candidate -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/gain-candidate --target render-gain
/tmp/gain-candidate/render-gain /tmp/gain-candidate/pcm
diff -qr /tmp/gain-reference/pcm /tmp/gain-candidate/pcm
```

[Recorded reference hashes](reference-macos-arm64.sha256) cover the renders from the studio's pre-extraction engines at f8f96ba, on Apple Silicon with AppleClang 21.0.0 and Release optimization. Configure both builds with `-DCMAKE_OSX_ARCHITECTURES=arm64` to reproduce that machine's build. Cross-platform math libraries can round differently. Use a reference and a candidate from the same target for a sample-by-sample comparison.
