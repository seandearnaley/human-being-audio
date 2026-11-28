// The set, in the top bar. Its name is edited in place, as a note's title is. The arrow beside it
// lists the other sets and makes a new one.
import { useEffect, useRef, useState } from "react";
import type { StageSet } from "./stage.d";

export function SetMenu({ set, onChange }: { set: StageSet; onChange(next: StageSet): void }) {
  const [name, setName] = useState(set.name);
  const [sets, setSets] = useState<{ file: string; name: string }[] | null>(null);   // null while the menu is shut
  const box = useRef<HTMLDivElement>(null);
  const field = useRef<HTMLInputElement>(null);
  const fresh = useRef(false);

  useEffect(() => setName(set.name), [set.file, set.name]);
  // A set just made: select its name, ready to be typed over. This waits for the field to show the
  // new name, because changing a field's text drops its selection.
  useEffect(() => {
    if (!fresh.current || name !== set.name) return;
    fresh.current = false;
    field.current?.focus();
    field.current?.select();
  }, [name, set.file, set.name]);
  useEffect(() => {
    if (!sets) return;
    const shut = (event: Event) => { if (event instanceof KeyboardEvent ? event.key === "Escape" : !box.current?.contains(event.target as Node)) setSets(null); };
    addEventListener("pointerdown", shut);
    addEventListener("keydown", shut);
    return () => { removeEventListener("pointerdown", shut); removeEventListener("keydown", shut); };
  }, [sets]);

  const rename = () => { if (name.trim() && name !== set.name) window.stage.call("set:rename", { name }).then(onChange); else setName(set.name); };
  const open = (file: string) => { setSets(null); window.stage.call("set:open", { file }).then(onChange); };
  const create = () => { setSets(null); fresh.current = true; window.stage.call("set:new").then(onChange); };

  return (
    <div className="set" ref={box}>
      <input ref={field} aria-label="Set name" value={name} size={Math.max(4, name.length)} spellCheck={false}
        onChange={(event) => setName(event.target.value)} onBlur={rename}
        onKeyDown={(event) => { if (event.key === "Enter") event.currentTarget.blur(); if (event.key === "Escape") { setName(set.name); event.currentTarget.blur(); } }} />
      <button aria-label="Sets" aria-haspopup="menu" aria-expanded={sets !== null} onClick={() => (sets ? setSets(null) : window.stage.call("set:list").then(setSets))}>
        <svg aria-hidden="true" width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"><path d="M3 4.5 6 7.5 9 4.5" /></svg>
      </button>
      {sets && (
        <div className="menu" role="menu">
          {sets.map((entry) => (
            <button key={entry.file} role="menuitemradio" aria-checked={entry.file === set.file} onClick={() => open(entry.file)}>{entry.name}</button>
          ))}
          <hr />
          <button role="menuitem" onClick={create}>New set</button>
        </div>
      )}
    </div>
  );
}
