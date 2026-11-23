# Catalogue audio examples

The three-second stereo examples are captured from the production engines at 48 kHz and a 64-frame host block. Livecut Redux uses a synthetic drum loop and the Rush pad. Tract Form plays MIDI note 29. Pole Field runs Tract Form's MIDI note 29 through Neuro Chew with inter-stage Buchla ADAA2 drive and Sub Lock. Cutline renders the Neuro Whip preset at 172 BPM through its built-in 4-voice poly audition synth. Playback is opt-in.

From the studio root, after running the study, convert each interleaved stereo render to 16-bit FLAC with a fixed −6 dB trim. For example:

```sh
ffmpeg -f f32le -ar 48000 -ac 2 -i /tmp/gain-candidate/pcm/livecut-redux-48000-64.f32 \
  -af volume=-6dB -sample_fmt s16 -map_metadata -1 web/public/audio/livecut-redux.flac
```

Use the corresponding Tract Form filename for the synth. The renderer and its stimulus are in `research/gain/`.
