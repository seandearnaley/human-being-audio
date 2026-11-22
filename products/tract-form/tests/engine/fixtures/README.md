# Fixed-lane reference renders

These two reference files were rendered at 48 kHz with MIDI note 29 held for 24,000 frames after upgrading Tract Form's voice folder to studio-shared second-order ADAA (`hba::dsp::Adaa2` with 1-sample matched sub delay and zero-origin centroid bias subtraction). Each file is two consecutive channels of little-endian float32. The test verifies the FNV-1a hashes on the stored samples before comparing both channels of a new render against a 1e-6 peak error bound and a 1e-7 stereo RMS bound. Changes to sound require review against the recorded references.
