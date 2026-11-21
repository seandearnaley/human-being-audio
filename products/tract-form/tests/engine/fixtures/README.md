# Fixed-lane reference renders

The original pre-migration Mac development build produced these two files at 48 kHz with MIDI note 29 held for 24,000 frames. Each file is two consecutive channels of little-endian float32. The test verifies the two original upstream FNV hashes on the stored samples before comparing both channels of a new render.

The largest absolute differences from the originals in Release were 1.20e-7 on Apple Silicon, 6.11e-7 on Windows x64 and 3.95e-7 on Ubuntu x86_64. Maximum measured stereo RMS error was 5.81e-8. The test checks every frame against a 1e-6 peak error bound and a 1e-7 stereo RMS bound. Changes to sound require review against the original recordings.
