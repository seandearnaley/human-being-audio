// The agent's two surfaces: the bar you type in, and the panel that shows what it made.
import { useState } from "react";
import type { Item, Made, MadeFile } from "./stage.d";
import type { Pad } from "./Rail";
import { OUTPUT } from "./Output";

// The file the agent made, if it made one.
export const fileOf = (made: Made | null | undefined): MadeFile | null => (made && !("error" in made) ? made : null);

export type Draft = { request: string; events: { seconds: number; what: string }[]; made: Made | null; target: Pad | null };

const Spark = () => (
  <svg aria-hidden="true" width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round">
    <path d="M8 1.5v3M8 11.5v3M1.5 8h3M11.5 8h3M3.6 3.6l1.9 1.9M10.5 10.5l1.9 1.9M12.4 3.6l-1.9 1.9M5.5 10.5l-1.9 1.9" />
  </svg>
);
const Check = ({ ok, children }: { ok: boolean; children: string }) => (
  <span className={ok ? "check" : "check failed"}>
    <svg aria-hidden="true" width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round">
      {ok ? <path d="M2.5 6.5 5 9 9.5 3.5" /> : <path d="M3 3l6 6M9 3l-6 6" />}
    </svg>{children}
  </span>
);

export function Prompt({ placeholder, busy, onSubmit }: { placeholder: string; busy: boolean; onSubmit(request: string): void }) {
  const [text, setText] = useState("");
  return (
    <form className={busy ? "prompt busy" : "prompt"} onSubmit={(event) => { event.preventDefault(); if (text.trim() && !busy) { onSubmit(text.trim()); setText(""); } }}>
      <Spark />
      <input aria-label={placeholder} placeholder={placeholder} value={text} disabled={busy} onChange={(event) => setText(event.target.value)} />
    </form>
  );
}

export function Making({ draft, names, keepLabel, onKeep, onDiscard }: { draft: Draft; names: Map<string, Item>; keepLabel: string; onKeep(): void; onDiscard(): void }) {
  const { made } = draft;
  if (!made) {
    const last = draft.events.at(-1);
    return <div className="making"><p>{draft.request}</p><div className="row"><span className="working">{last ? `${last.what} · ${Math.round(last.seconds)} s` : "Writing"}</span></div></div>;
  }
  if ("error" in made) return <div className="making"><p>{draft.request}</p><div className="row"><span className="check failed">{made.error}</span><div className="actions"><button onClick={onDiscard}>Close</button></div></div></div>;
  const checks = made.checks;
  const item = names.get(made.url);
  return (
    <div className="making">
      <p>{made.said || draft.request}</p>
      <div className="row">
        {made.kind === "cuts" && item?.thumb && <img className="cut-frame" src={OUTPUT + item.thumb} alt="The middle of the cut" />}
        <strong>{item?.name ?? ""}</strong>
        {made.kind === "scenes" && <Check ok={checks?.followsDrums === true}>On the beat</Check>}
        {made.kind === "cuts" && <Check ok={(checks?.errors?.length ?? 1) === 0}>Compiles</Check>}
        <Check ok={checks?.sameTwice === true}>Same every render</Check>
        <code>{Math.round(made.seconds)} s</code>
        <div className="actions"><button onClick={onDiscard}>Discard</button><button className="primary" onClick={onKeep}>{keepLabel}</button></div>
      </div>
    </div>
  );
}
