# Stage plugins

Two plugins that feed the Stage desktop app from a DAW. Both are built from the same sources; the
compile-time switch is `JucePlugin_IsSynth`.

- **Stage Link** (instrument, `Stg1`). Put it on a track. It makes no sound and reports that track's
  transport, MIDI notes and eight macro knobs. Its editor chooses what the track plays: Drums or
  Layer 1 to 4. A new instance takes the lowest free layer.
- **Stage Sound** (effect, `Stg2`). Put it last on the main track. It passes the audio through
  untouched and streams it to the Stage.

Formats are VST3 and AU. The editor also shows "Connected" or "Not connected" and, on macOS, an
"Open Stage" button that runs `/usr/bin/open -a Stage`.

## How it talks to the Stage

Data goes one way only, from the plugin to the app. Nothing comes back.

The Stage app listens on two loopback TCP ports (its "hub") and writes them to `hub.json` in its
Electron `userData` folder:

| Platform | Path                                           |
| -------- | ---------------------------------------------- |
| macOS    | `~/Library/Application Support/Stage/hub.json` |
| Windows  | `%APPDATA%/Stage/hub.json`                     |
| Linux    | `~/.config/Stage/hub.json`                     |

```json
{ "port": 51234, "soundPort": 51235, "token": "…" }
```

Each plugin instance reads that file and connects out to `127.0.0.1`. While there is no hub it tries
again once a second. The audio thread only pushes into two lock-free rings (`Capture`, `SampleRing`);
a pump thread drains them every 2 ms and does all the I/O.

### Block lines (`port`)

One JSON object per line, about one per audio block:

| Field     | Meaning                                                                               |
| --------- | ------------------------------------------------------------------------------------- |
| `token`   | The token from `hub.json`. Always the first field.                                    |
| `id`      | This instance: 32 hex digits, random, new each time the plugin is loaded.             |
| `role`    | 0 drums, 1 to 4 layers, 5 the song's sound (Stage Sound).                             |
| `us`      | Wall clock at the start of the block, microseconds since the Unix epoch.              |
| `ppq`     | Position in quarter notes at the start of the block.                                  |
| `frame`   | Index of the block's first sample in the sound stream. Present only with audio input. |
| `bpm`     | Tempo.                                                                                |
| `playing` | Whether the transport is running.                                                     |
| `sig`     | Time signature, `[numerator, denominator]`.                                           |
| `level`   | RMS of the block's input.                                                             |
| `macros`  | The eight macro values, 0 to 1.                                                       |
| `notes`   | Note events since the last line: `[on, note, channel, velocity, ppq, us]`, `on` 1 or 0. |
| `held`    | Notes held now: `[note, channel, ppq of the note-on]`.                                |

If several blocks are waiting, the line carries the newest block and every note since the last line.

### Sound stream (`soundPort`)

Opened only by an instance that has audio input. First one header line:

```json
{"token":"…","id":"…","sound":{"channels":2,"frame":0,"rate":48000.0}}
```

then raw 32-bit float samples in native byte order, interleaved stereo, until the connection closes.
`frame` is the index of the first sample that follows, on the same count as `frame` in the block
lines. After a reconnect a new header says where the stream picks up.

### Lost connections

A write to a hub that has gone away never raises `SIGPIPE` in the host: macOS sets `SO_NOSIGPIPE` on
the socket, Linux sends with `MSG_NOSIGNAL`, Windows has no such signal. On macOS and Linux a write
that makes no progress for two seconds also counts as a lost connection, so a Stage that has stopped
reading cannot hold the plugin up.

## Build and test

Needs CMake 3.25 or later, Ninja and a Clang that enforces `[[clang::nonblocking]]`.

```bash
cd products/stage/plugin
cmake --preset dev -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake --build --preset dev
ctest --preset dev
```

This builds both plugins under `build/dev/*_artefacts/` and the headless host
`build/dev/stage-link-host`. Add `-DSTAGE_INSTALL_PLUGIN=ON` to the first command to also copy the
plugins into `~/Library/Audio/Plug-Ins` after each build.

Without presets:

```bash
cmake -B build/release -G Ninja -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"   # Release unless CMAKE_BUILD_TYPE is set
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

### Test host

`stage-link-host` is a stand-in DAW. It runs the real processor at real-time pace with a playing
transport, prints `READY` once the processors are prepared, and ends with
`DONE dropped=<n> onHub=<n>` (events the audio thread had to drop, and instances connected to a hub).

```bash
stage-link-host [seconds] [bpm] [blockSize]          # one track: drums and a scene clip together
stage-link-host [seconds] [bpm] [blockSize] tracks   # three tracks: drums with sound, layer 1, layer 2
```

The ctest runs it for one second in `tracks` mode and expects a clean exit and the `DONE` line. It
needs no hub. If the Stage app happens to be running, the host connects to it like any plugin would.
