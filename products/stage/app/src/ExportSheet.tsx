// A take to video: pick the take, the size and the frame rate.
import { useEffect, useState } from "react";
import type { Exported, Take } from "./stage.d";

const length = (seconds: number | null) => (seconds === null ? "no sound" : `${Math.floor(seconds / 60)}:${String(Math.round(seconds % 60)).padStart(2, "0")}`);

export function ExportSheet({ onClose }: { onClose(): void }) {
  const [takes, setTakes] = useState<Take[] | null>(null);
  const [take, setTake] = useState("");
  const [height, setHeight] = useState(1080);
  const [fps, setFps] = useState(60);
  const [state, setState] = useState<"idle" | "working" | Exported>("idle");

  useEffect(() => { window.stage.call("take:list").then((list) => { setTakes(list); setTake(list[0]?.name ?? ""); }); }, []);
  const run = async () => { setState("working"); setState(await window.stage.call("take:export", { take, height, fps })); };
  const done = typeof state === "object" ? state : null;
  const file = done && !("error" in done) ? done : null;
  const failed = done && "error" in done ? done.error : null;

  return (
    <div className="scrim" onClick={(event) => event.target === event.currentTarget && state !== "working" && onClose()}>
      <form className="sheet" role="dialog" aria-label="Export" onSubmit={(event) => { event.preventDefault(); run(); }}>
        <h1>Export</h1>
        {takes?.length === 0 && <p className="quiet">No takes yet. Press Record while the song plays.</p>}
        {takes && takes.length > 0 && (
          <fieldset>
            <legend>Take</legend>
            {takes.slice(0, 4).map((entry) => (
              <label key={entry.name} className={entry.name === take ? "choice on" : "choice"}>
                <input type="radio" name="take" checked={entry.name === take} onChange={() => setTake(entry.name)} />
                <span>{entry.name}</span><code>{length(entry.seconds)}</code>
              </label>
            ))}
          </fieldset>
        )}
        <div className="pair">
          <div><span id="size">Size</span>
            <div className="segments" role="group" aria-labelledby="size">
              {[1080, 2160].map((value) => <button type="button" key={value} aria-pressed={height === value} onClick={() => setHeight(value)}>{value}p</button>)}
            </div>
          </div>
          <div><span id="rate">Frames a second</span>
            <div className="segments" role="group" aria-labelledby="rate">
              {[30, 60, 90, 120].map((value) => <button type="button" key={value} aria-pressed={fps === value} onClick={() => setFps(value)}>{value}</button>)}
            </div>
          </div>
        </div>
        <div className="row">
          <code>{state === "working" ? "Exporting" : failed ?? (file ? `${file.megabytes} MB · ${file.framesPerSecond} frames a second` : "")}</code>
          <div className="actions">
            <button type="button" onClick={onClose} disabled={state === "working"}>{file ? "Done" : "Cancel"}</button>
            {file
              ? <button type="button" className="primary" onClick={() => window.stage.call("reveal", { file: file.file })}>Show in Finder</button>
              : <button type="submit" className="primary" disabled={!take || state === "working"}>Export</button>}
          </div>
        </div>
      </form>
    </div>
  );
}
