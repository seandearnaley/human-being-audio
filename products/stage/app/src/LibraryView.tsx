// Everything in the library folder. A scene goes onto the next free pad of the layer in hand; a
// Cut becomes the set's Cut.
import { useState } from "react";
import type { Library } from "./stage.d";
import { OUTPUT } from "./Output";

export function LibraryView({ library, layerName, cut, onScene, onCut }: { library: Library; layerName: string; cut: string | null; onScene(url: string): void; onCut(url: string): void }) {
  const [kind, setKind] = useState<"scenes" | "cuts">("scenes");
  const [find, setFind] = useState("");
  const items = library[kind].filter((item) => item.name.toLowerCase().includes(find.toLowerCase()));
  return (
    <div className="library">
      <div className="library-bar">
        <div className="tabs" role="tablist">
          <button role="tab" aria-selected={kind === "scenes"} onClick={() => setKind("scenes")}>Scenes</button>
          <button role="tab" aria-selected={kind === "cuts"} onClick={() => setKind("cuts")}>Cuts</button>
        </div>
        <input aria-label="Search the library" placeholder="Search" value={find} onChange={(event) => setFind(event.target.value)} />
      </div>
      <div className="tiles">
        {items.map((item) => (
          <button key={item.url} className={item.url === cut ? "tile on" : "tile"} aria-label={kind === "scenes" ? `${item.name}, add to ${layerName}` : `${item.name}, use as the cut`} onClick={() => (kind === "scenes" ? onScene(item.url) : onCut(item.url))}>
            {item.thumb ? <img src={OUTPUT + item.thumb} alt="" /> : <div className="no-thumb" />}
            <span>{item.name}</span>
          </button>
        ))}
      </div>
    </div>
  );
}
