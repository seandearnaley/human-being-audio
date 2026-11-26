import { useEffect, useMemo, useRef, useState } from "react";
import type { Library, Made, Showing, StageSet } from "./stage.d";
import { useHub } from "./useHub";
import { OUTPUT, Output, type OutputHandle } from "./Output";
import { Rail, noteName, type Pad } from "./Rail";
import { Knobs } from "./Knobs";
import { Making, Prompt, fileOf, type Draft } from "./Prompt";
import { LibraryView } from "./LibraryView";
import { ExportSheet } from "./ExportSheet";
import { SetMenu } from "./SetMenu";

const clock = (seconds: number) => `${Math.floor(seconds / 60)}:${String(Math.floor(seconds % 60)).padStart(2, "0")}`;

export function App() {
  const [set, setSet] = useState<StageSet | null>(null);
  const [library, setLibrary] = useState<Library>({ scenes: [], cuts: [], looks: [] });
  const [version, setVersion] = useState(0);              // bumps when the set changes, to reload the picture
  const [showing, setShowing] = useState<Showing>([]);
  const [layer, setLayer] = useState(0);                  // the layer the knobs show, and where new scenes go
  const [picked, setPicked] = useState<Pad[]>([]);        // up to two pads: what the agent will work on
  const [view, setView] = useState<"stage" | "library" | "export">("stage");
  const [draft, setDraft] = useState<Draft | null>(null);
  const [recording, setRecording] = useState<number | null>(null);   // when the take started
  const [, tick] = useState(0);
  const output = useRef<OutputHandle>(null);
  const transport = useHub((line) => output.current?.line(line));

  const refresh = (next: StageSet) => { setSet(next); setVersion((n) => n + 1); window.stage.call("library:list").then(setLibrary); };
  useEffect(() => { window.stage.call("set:get").then(refresh); }, []);
  useEffect(() => {
    if (recording === null) return;
    const timer = setInterval(() => tick((n) => n + 1), 500);
    return () => clearInterval(timer);
  }, [recording]);

  const names = useMemo(() => new Map([...library.scenes, ...library.cuts, ...library.looks].map((item) => [item.url, item])), [library]);
  if (!set) return null;

  // The picture: the set, or the scene just made, playing against whatever the DAW is doing.
  const made = fileOf(draft?.made);
  const preview = made?.kind === "scenes" ? made.url : null;
  const src = preview
    ? `${OUTPUT}/index.html?live=port&fx=0&layers=${encodeURIComponent(preview)}`
    : `${OUTPUT}/index.html?live=port&fx=${set.look ? 1 : 0}&set=${encodeURIComponent(`/home/sets/${set.file}`)}&v=${version}`;

  const pick = (pad: Pad) => {
    setLayer(pad.layer);
    setPicked((now) => (now.some((p) => p.layer === pad.layer && p.note === pad.note) ? now.filter((p) => !(p.layer === pad.layer && p.note === pad.note)) : [...now, pad].slice(-2)));
    if (!transport.connected) output.current?.audition(pad.layer, pad.note);
  };

  // What is picked decides what the agent makes.
  const make = async (request: string) => {
    const [first, second] = picked;
    const job = first && second ? { pair: [first.url, second.url] as [string, string] } : first ? { change: first.url } : {};
    setDraft({ request, events: [], made: null, target: first && !second ? first : null });
    const stop = window.stage.onAgentEvent((event) => setDraft((now) => now && { ...now, events: [...now.events, event] }));
    const result: Made = await window.stage.call("agent:make", { request, ...job });
    stop();
    setDraft((now) => now && { ...now, made: result });
    window.stage.call("library:list").then(setLibrary);
  };
  const keep = async () => {
    if (!made) return;
    const next = made.kind === "cuts"
      ? await window.stage.call("set:choose", { cut: made.url })
      : await window.stage.call("set:assign", { layer: draft?.target?.layer ?? layer, note: draft?.target?.note, url: made.url });
    setDraft(null);
    setPicked([]);
    refresh(next);
  };
  const discard = async () => {
    if (made) await window.stage.call("agent:discard", { url: made.url });
    setDraft(null);
    window.stage.call("library:list").then(setLibrary);
  };
  const record = async () => {
    if (recording === null) { await window.stage.call("take:record", { soundId: transport.soundId }); setRecording(Date.now()); }
    else { await window.stage.call("take:finish"); setRecording(null); }
  };

  const free = (() => { let note = 60; while (set.layers[layer]?.scenes[note]) note += 1; return note; })();
  const asks = picked.length === 2 ? `Make a cut from ${names.get(picked[0]!.url)?.name} to ${names.get(picked[1]!.url)?.name}`
    : picked.length === 1 ? `Change ${names.get(picked[0]!.url)?.name}` : "Make a scene";

  return (
    <div className="app">
      <header className="bar">
        <SetMenu set={set} onChange={(next) => { if (next.file !== set.file) { setPicked([]); setDraft(null); setLayer(0); setView("stage"); } refresh(next); }} />
        <div className="transport">
          <span className={transport.connected ? "link on" : "link"}><i />{transport.connected ? "Connected" : "No plugin"}</span>
          {transport.connected && <span>{transport.bpm} bpm</span>}
          {transport.connected && <span className="position">{transport.position}</span>}
        </div>
        <nav>
          <button onClick={record} aria-pressed={recording !== null} disabled={!transport.connected && recording === null}>
            <i className={recording !== null ? "dot live" : "dot"} />{recording !== null ? clock((Date.now() - recording) / 1000) : "Record"}
          </button>
          <button onClick={() => setView(view === "library" ? "stage" : "library")} aria-pressed={view === "library"}>Library</button>
          <button onClick={() => setView("export")}>Export</button>
        </nav>
      </header>

      <div className="body">
        <Rail set={set} names={names} showing={showing} transport={transport} layer={layer} picked={picked} landing={made?.kind === "scenes" && !draft?.target ? free : null}
          onLayer={setLayer} onPick={pick} onEmpty={(index) => { setLayer(index); setView("library"); }}
          onCut={() => { const urls = library.cuts.map((c) => c.url); window.stage.call("set:choose", { cut: urls[(urls.indexOf(set.cut ?? "") + 1) % urls.length] ?? null }).then(refresh); }}
          onLook={() => { const urls = [null, ...library.looks.map((l) => l.url)]; window.stage.call("set:choose", { look: urls[(urls.indexOf(set.look) + 1) % urls.length] ?? null }).then(refresh); }} />

        <main className="main">
          {view === "library"
            ? <LibraryView library={library} layerName={set.layers[layer]?.name ?? ""} cut={set.cut}
                onScene={(url) => window.stage.call("set:assign", { layer, url }).then((next) => { refresh(next); setView("stage"); })}
                onCut={(url) => window.stage.call("set:choose", { cut: url }).then(refresh)} />
            : <Output ref={output} src={src} onShowing={setShowing} />}
          {draft
            ? <Making draft={draft} names={names} keepLabel={made?.kind === "cuts" ? "Keep as the cut" : `Keep on ${noteName(draft.target?.note ?? free)}`} onKeep={keep} onDiscard={discard} />
            : <Knobs title={set.layers[layer]?.name ?? ""} scene={names.get(set.layers[layer]?.scenes[showing[layer]?.to ?? -1] ?? "")?.name ?? ""} names={showing[layer]?.knobs ?? []} values={transport.knobs[layer + 1] ?? []} />}
          <Prompt placeholder={asks} busy={draft !== null && draft.made === null} onSubmit={make} />
        </main>
      </div>

      {view === "export" && <ExportSheet onClose={() => setView("stage")} />}
    </div>
  );
}
