# Stage

Visuals played by notes. An app that draws tempo-locked scenes, and two plugins that tell it what the DAW is doing.

Not in `studio.json` yet, so the studio's CI and release tooling ignore it.

## The parts

| Folder | What it is |
| --- | --- |
| `app/` | The Stage app: Electron, with the page in React. |
| `app/main/` | The main process, in plain modules: the hub, the files, the agent, checks, export. |
| `app/engine/` | The output page: the compositor that draws scenes. Plain files, never bundled. |
| `app/src/` | The app's own page: rail, picture, knobs, prompt. |
| `app/skill/` | What the agent is told: the scene contract and the Cut contract. |
| `app/starter/` | The set and files copied in on first run. |
| `plugin/` | Stage Link (instrument, one a track) and Stage Sound (effect, on the main track). |

## How it fits together

- **Five things exist.** A Scene is one HTML file. A Cut and a Look are one WGSL function each. A Set says which note plays which scene. A Take is one recorded play-through.
- **Four rules play them.** A track is a layer. A note is a scene. A note that starts while the last is held is a Cut. Each layer has eight knobs.
- **The plugins connect to the app.** The app runs a hub on loopback and writes its ports and a token to `hub.json` in its data folder. Each plugin instance connects and sends one line per audio block.
- **Scenes cannot reach the app.** The picture runs in a frame on its own origin (`stage://output`) with no bridge to the main process. The app's page (`stage://app`) is the only one with a bridge.
- **Everything a person owns is plain files** in `~/Music/Stage`: `library/`, `sets/`, `takes/`, `exports/`.
- **A set's file name never changes.** Its own name is inside the file, so renaming a set does not break the takes that refer to it.

## Run it

```bash
cd products/stage/app
npm install
npm run dev        # the app, with hot reload
npm start          # the app, built
npm test           # unit tests
npm run typecheck
npm run e2e        # starts the real app on an empty folder and drives it
```

`STAGE_HOME=/some/folder` keeps the app away from `~/Music/Stage`. The agent needs `ANTHROPIC_API_KEY`; `STAGE_MODEL` picks the model.

The end-to-end test takes two switches: `HOST=<path to stage-link-host>` plays three tracks into the app, records a take and exports it, and `AGENT=1` asks the agent for a scene, which spends on the key.

## Not built yet

- Deleting a set, and adding or naming layers. A new set has two layers, Back and Front.
- More than two layers at once, and a Cut or a Look per layer. A set has one of each.
- Storing a model key, and any provider but Anthropic.
- Packaging, signing and updates. The plugin's "Open Stage" button expects an installed app.
- Windows and Linux.
