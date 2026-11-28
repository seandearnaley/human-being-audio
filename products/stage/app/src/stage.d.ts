// The shapes that cross between the app's page, the main process and the output frame.
export type Layer = { name: string; cutBeats: number; scenes: Record<string, string> };
export type StageSet = { file: string; name: string; cut: string | null; look: string | null; layers: Layer[] };
export type Item = { kind: "scenes" | "cuts" | "looks"; url: string; name: string; bars: number | null; thumb: string | null };
export type Library = { scenes: Item[]; cuts: Item[]; looks: Item[] };
export type Checks = { passed: boolean; lint?: string[]; errors?: string[]; blank?: boolean; followsDrums?: boolean; sameTwice?: boolean; changesAcrossTheCut?: boolean; scriptMsPerFrame?: number };
export type MadeFile = { url: string; kind: "scenes" | "cuts"; passed: boolean; said: string; seconds: number; saves: number; checks: Checks | null };
export type Made = MadeFile | { error: string };
export type Take = { name: string; set: string | null; made: number; seconds: number | null };
export type ExportedFile = { file: string; frames: number; framesPerSecond: number; megabytes: number };
export type Exported = ExportedFile | { error: string };
// What the output frame says is on screen: per layer, the notes showing and the knobs its scene declares.
export type Showing = { from: number | null; to: number | null; knobs: string[] }[];

type Calls = {
  "set:get": [undefined, StageSet];
  "set:list": [undefined, { file: string; name: string }[]];
  "set:open": [{ file: string }, StageSet];
  "set:new": [undefined, StageSet];
  "set:rename": [{ name: string }, StageSet];
  "set:assign": [{ layer: number; note?: number; url: string }, StageSet];
  "set:choose": [{ cut?: string | null; look?: string | null }, StageSet];
  "library:list": [undefined, Library];
  "agent:make": [{ request: string; change?: string; pair?: [string, string] }, Made];
  "agent:discard": [{ url: string }, void];
  "take:record": [{ soundId: string | null }, { name: string }];
  "take:finish": [undefined, { lines: number; seconds: number | null }];
  "take:list": [undefined, Take[]];
  "take:export": [{ take: string; height: number; fps: number }, Exported];
  reveal: [{ file: string }, void];
};

declare global {
  interface Window {
    stage: {
      call<Name extends keyof Calls>(name: Name, ...payload: Calls[Name][0] extends undefined ? [] : [Calls[Name][0]]): Promise<Calls[Name][1]>;
      onLine(handler: (line: string) => void): () => void;
      onAgentEvent(handler: (event: { seconds: number; what: string }) => void): () => void;
    };
  }
}
