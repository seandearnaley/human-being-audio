// Everything a person owns in Stage is plain files under one folder (~/Music/Stage):
//   library/scenes/*.html   library/cuts/*.wgsl   library/looks/*.wgsl   each with an optional .jpg beside it
//   sets/<name>.json        per layer, which note plays which scene; plus the set's Cut and Look.
//                           The file's name never changes, so takes can refer to it; the set's own
//                           name is inside and can be changed freely.
//   takes/<name>.take       a recorded play-through, with <name>.wav if a track had sound
//   exports/*.mp4
// The output page reads these at /home/<path>. Nothing outside the folder is ever served.
import fs from "node:fs/promises";
import path from "node:path";

const KINDS = { scenes: ".html", cuts: ".wgsl", looks: ".wgsl" };
const words = (name) => name.replace(/[-_]+/g, " ").replace(/\b\w/g, (letter) => letter.toUpperCase());
const exists = (file) => fs.access(file).then(() => true, () => false);

export async function openLibrary(home, starter) {
  for (const folder of ["library/scenes", "library/cuts", "library/looks", "sets", "takes", "exports", ".drafts"]) await fs.mkdir(path.join(home, folder), { recursive: true });
  // First run: the starter set and the files it uses.
  if (!(await fs.readdir(path.join(home, "sets"))).length) await fs.cp(starter, home, { recursive: true });

  const file = (...parts) => path.join(home, ...parts);
  const address = (...parts) => `/home/${parts.join("/")}`;

  // A scene says its own name in its manifest. Anything else is named after its file.
  async function describe(kind, name) {
    const source = await fs.readFile(file("library", kind, name + KINDS[kind]), "utf8");
    const manifest = kind === "scenes" ? JSON.parse(/<script[^>]*id=["']scene["'][^>]*>([\s\S]*?)<\/script>/.exec(source)?.[1] ?? "{}") : {};
    return {
      kind, url: address("library", kind, name + KINDS[kind]), name: manifest.name ?? words(name), bars: manifest.bars ?? null,
      thumb: (await exists(file("library", kind, `${name}.jpg`))) ? address("library", kind, `${name}.jpg`) : null,
    };
  }

  return {
    home, file, address, kinds: KINDS,
    // The file behind a /home/... address, or null if the address leaves the folder.
    resolve(pathname) {
      const target = path.normalize(path.join(home, decodeURIComponent(pathname.replace(/^\/home\//, ""))));
      return target.startsWith(home + path.sep) ? target : null;
    },
    async list() {
      const all = {};
      for (const kind of Object.keys(KINDS)) {
        const names = (await fs.readdir(file("library", kind))).filter((name) => name.endsWith(KINDS[kind])).sort();
        all[kind] = await Promise.all(names.map((name) => describe(kind, name.slice(0, -KINDS[kind].length))));
      }
      return all;
    },
    async sets() {
      return (await fs.readdir(file("sets"))).filter((name) => name.endsWith(".json")).sort();
    },
    // A new, empty set: two layers, the first Cut in the library, no Look. Returns its file name.
    async createSet() {
      const taken = new Set(await fs.readdir(file("sets")));
      let name = "untitled.json";
      for (let n = 2; taken.has(name); n += 1) name = `untitled-${n}.json`;
      const cuts = (await fs.readdir(file("library", "cuts"))).filter((cut) => cut.endsWith(".wgsl")).sort();
      const first = cuts.includes("push.wgsl") ? "push.wgsl" : cuts[0];
      await fs.writeFile(file("sets", name), `${JSON.stringify({
        name: "Untitled", cut: first ? address("library", "cuts", first) : null, look: null,
        layers: [{ name: "Back", cutBeats: 2, scenes: {} }, { name: "Front", cutBeats: 1, scenes: {} }],
      }, null, 2)}\n`);
      return name;
    },
    // Which set was open last, so the app comes back to it.
    async lastSet() {
      const remembered = JSON.parse(await fs.readFile(file(".stage.json"), "utf8").catch(() => "{}")).set;
      const all = await this.sets();
      return all.includes(remembered) ? remembered : all[0];
    },
    rememberSet: (name) => fs.writeFile(file(".stage.json"), JSON.stringify({ set: name })),
    readSet: async (name) => JSON.parse(await fs.readFile(file("sets", name), "utf8")),
    writeSet: (name, set) => fs.writeFile(file("sets", name), `${JSON.stringify(set, null, 2)}\n`),
    // Takes, newest first, each with its length when it has sound.
    async takes() {
      const names = (await fs.readdir(file("takes"))).filter((name) => name.endsWith(".take"));
      const takes = await Promise.all(names.map(async (name) => {
        const handle = await fs.open(file("takes", name));
        const { value: first } = await handle.readLines()[Symbol.asyncIterator]().next();
        await handle.close();
        const header = JSON.parse(first);
        const stat = await fs.stat(file("takes", name));
        const sound = header.sound && (await fs.stat(file("takes", header.sound.file)).catch(() => null));
        return {
          name: name.slice(0, -5), set: header.set?.name ?? header.set ?? null, made: stat.mtimeMs,
          seconds: sound ? (sound.size - 44) / (header.sound.channels * 4) / header.sound.rate : null,
        };
      }));
      return takes.sort((a, b) => b.made - a.made);
    },
  };
}
