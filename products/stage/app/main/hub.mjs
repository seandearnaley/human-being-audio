// The hub: every Stage Link instance connects here. Loopback only. The ports and a token go in a
// file only this user can read; anything that does not start with the token ends its connection.
//   lines port: one JSON line per audio block (transport, notes, knobs), passed on unparsed.
//   sound port: one header line, then the sound at that plugin's input as raw float samples.
// It also records takes: the lines as they arrived, plus the sound from the same moment as a WAV.
import net from "node:net";
import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";
import { randomBytes } from "node:crypto";

const listen = (server) => new Promise((resolve) => server.listen(0, "127.0.0.1", () => resolve(server.address().port)));
const finished = (stream) => new Promise((resolve) => stream.end(resolve));

function wavHeader(rate, channels, bytes) {
  const header = Buffer.alloc(44);
  header.write("RIFF", 0); header.writeUInt32LE(36 + bytes, 4); header.write("WAVEfmt ", 8);
  header.writeUInt32LE(16, 16); header.writeUInt16LE(3, 20); header.writeUInt16LE(channels, 22);   // 3 = float
  header.writeUInt32LE(rate, 24); header.writeUInt32LE(rate * channels * 4, 28); header.writeUInt16LE(channels * 4, 32);
  header.writeUInt16LE(32, 34); header.write("data", 36); header.writeUInt32LE(bytes, 40);
  return header;
}

export async function startHub(file, deliver) {
  const token = randomBytes(16).toString("hex");
  const prefix = `{"token":"${token}",`;
  const sockets = new Set();
  const sources = new Map();   // plugin id -> its sound: { rate, channels, bytes } with bytes the stream position so far
  let take = null;
  const accept = (socket) => {
    sockets.add(socket);
    socket.setNoDelay(true);
    socket.on("error", () => {});
    socket.on("close", () => sockets.delete(socket));
  };

  const lines = net.createServer((socket) => {
    accept(socket);
    readline.createInterface({ input: socket }).on("line", (line) => {
      if (!line.startsWith(prefix)) return socket.destroy();
      deliver(line);
      if (take) { take.events.write(`{${line.slice(prefix.length)}\n`); take.lines += 1; }
    });
  });

  const sound = net.createServer((socket) => {
    accept(socket);
    let head = Buffer.alloc(0);
    let source = null;
    socket.on("data", (chunk) => {
      if (!source) {
        head = Buffer.concat([head, chunk]);
        const end = head.indexOf(10);
        if (end < 0) return;
        const line = head.subarray(0, end).toString();
        if (!line.startsWith(prefix)) return socket.destroy();
        const { id, sound: { rate, channels, frame } } = JSON.parse(line);
        source = { id, rate, channels, bytes: frame * channels * 4 };
        sources.set(id, source);
        socket.on("close", () => sources.delete(id));
        chunk = head.subarray(end + 1);
      }
      // A take keeps this plugin's sound from the stream position it started on.
      if (take?.sound?.id === source.id) {
        const kept = chunk.subarray(Math.min(chunk.length, Math.max(0, take.sound.from - source.bytes)));
        take.wav.write(kept);
        take.sound.bytes += kept.length;
      }
      source.bytes += chunk.length;
    });
  });

  await fs.promises.writeFile(file, JSON.stringify({ port: await listen(lines), soundPort: await listen(sound), token }), { mode: 0o600 });

  return {
    // Start a take: <name>.take holds a header and then every line, <name>.wav the sound of the
    // plugin the app names (it knows which tracks are playing; the hub never parses lines).
    // `about` goes into the header, so a take says which set it was played on.
    async record(folder, name, soundId, about = {}) {
      await fs.promises.mkdir(folder, { recursive: true });
      const source = sources.get(soundId);
      const frameBytes = source ? source.channels * 4 : 0;
      const from = source ? Math.ceil(source.bytes / frameBytes) * frameBytes : 0;
      const header = { take: 1, ...about, sound: source && { id: source.id, file: `${name}.wav`, rate: source.rate, channels: source.channels, frame: from / frameBytes } };
      const events = fs.createWriteStream(path.join(folder, `${name}.take`));
      events.write(`${JSON.stringify(header)}\n`);
      const wav = source && fs.createWriteStream(path.join(folder, header.sound.file));
      wav?.write(wavHeader(source.rate, source.channels, 0));
      take = { folder, events, wav, lines: 0, sound: source && { id: source.id, from, bytes: 0, frameBytes, rate: source.rate, channels: source.channels, file: header.sound.file } };
    },
    async finish() {
      const done = take;
      take = null;
      await finished(done.events);
      if (!done.wav) return { lines: done.lines, seconds: null };
      await finished(done.wav);
      // Whole frames only, and the real sizes in the header.
      const bytes = done.sound.bytes - (done.sound.bytes % done.sound.frameBytes);
      const wavFile = path.join(done.folder, done.sound.file);
      await fs.promises.truncate(wavFile, 44 + bytes);
      const handle = await fs.promises.open(wavFile, "r+");
      await handle.write(wavHeader(done.sound.rate, done.sound.channels, bytes), 0, 44, 0);
      await handle.close();
      return { lines: done.lines, seconds: bytes / done.sound.frameBytes / done.sound.rate };
    },
    get recording() {
      return take !== null;
    },
    // Stopping: hang up on every plugin, or the process cannot exit while they are connected.
    async stop() {
      for (const socket of sockets) socket.destroy();
      lines.close();
      sound.close();
      await fs.promises.rm(file, { force: true });
    },
  };
}
