// The set, down the left: the drums, then each layer as a rack of pads. A pad is a note, and a
// note is a scene, so this is the whole map from what you play to what you see.
import type { Item, Showing, StageSet } from "./stage.d";
import type { Transport } from "./useHub";
import { OUTPUT } from "./Output";

export type Pad = { layer: number; note: number; url: string };
const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const noteName = (note: number) => `${NAMES[note % 12]}${Math.floor(note / 12) - 2}`;   // 60 is C3, as in Live

type Props = {
  set: StageSet; names: Map<string, Item>; showing: Showing; transport: Transport; layer: number; picked: Pad[]; landing: number | null;
  onLayer(layer: number): void; onPick(pad: Pad): void; onEmpty(layer: number): void; onCut(): void; onLook(): void;
};

export function Rail({ set, names, showing, transport, layer, picked, landing, onLayer, onPick, onEmpty, onCut, onLook }: Props) {
  return (
    <aside className="rail">
      <div className="drums">
        <strong>Drums</strong>
        <span className={transport.hit.kick ? "hit on" : "hit"}><i />Kick</span>
        <span className={transport.hit.snare ? "hit on" : "hit"}><i />Snare</span>
        <span className={transport.hit.hat ? "hit on" : "hit"}><i />Hat</span>
      </div>

      {set.layers.map((entry, index) => {
        const notes = Object.keys(entry.scenes).map(Number).sort((a, b) => a - b);
        const next = index === layer && landing !== null ? landing : null;
        // Always at least one empty pad to fill, rounded up to whole rows of four.
        const used = notes.length + (next === null ? 0 : 1);
        const empty = Math.ceil((used + 1) / 4) * 4 - used;
        const playing = showing[index]?.to ?? null;
        return (
          <section key={entry.name} className="layer">
            <button className={index === layer ? "layer-name on" : "layer-name"} onClick={() => onLayer(index)}>
              <span className="number">{index + 1}</span><strong>{entry.name}</strong>
              <span className="now">{playing === null ? "" : names.get(entry.scenes[playing] ?? "")?.name}</span>
            </button>
            <div className="pads">
              {notes.map((note) => {
                const url = entry.scenes[note]!;
                const item = names.get(url);
                const chosen = picked.some((p) => p.layer === index && p.note === note);
                return (
                  <button key={note} className={`pad${playing === note ? " playing" : ""}${chosen ? " picked" : ""}`} aria-pressed={chosen} aria-label={`${noteName(note)}, ${item?.name ?? "scene"}`} onClick={() => onPick({ layer: index, note, url })}>
                    {item?.thumb && <img src={OUTPUT + item.thumb} alt="" />}
                    <span>{noteName(note)}</span>
                  </button>
                );
              })}
              {next !== null && <div className="pad landing"><span>{noteName(next)}</span></div>}
              {Array.from({ length: empty }, (_, i) => <button key={i} className="pad empty" aria-label={`Add a scene to ${entry.name}`} onClick={() => onEmpty(index)} />)}
            </div>
          </section>
        );
      })}

      <div className="finish">
        <button onClick={onCut}><span>Cut</span>{names.get(set.cut ?? "")?.name ?? "None"}</button>
        <button onClick={onLook}><span>Look</span>{names.get(set.look ?? "")?.name ?? "None"}</button>
      </div>
    </aside>
  );
}
