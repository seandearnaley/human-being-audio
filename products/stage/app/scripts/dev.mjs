// One command for development: Vite serves the app's page with hot reload, and Electron loads it.
import { createServer } from "vite";
import { spawn } from "node:child_process";
import path from "node:path";

const server = await createServer({ root: path.join(import.meta.dirname, "..") });
await server.listen();
const electron = spawn(path.join(import.meta.dirname, "../node_modules/.bin/electron"), [path.join(import.meta.dirname, "..")], {
  stdio: "inherit", env: { ...process.env, STAGE_DEV_URL: server.resolvedUrls.local[0] },
});
electron.on("close", () => server.close().then(() => process.exit(0)));
