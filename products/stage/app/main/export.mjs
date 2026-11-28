// A take to video. The output page replays the take on its stepped clock in an offscreen window,
// which paints without a display: faster than real time, and the same pixels on every machine.
import { BrowserWindow } from "electron";

export async function exportTake({ origin, takeUrl, setUrl, look, width, height, fps, name }) {
  const window = new BrowserWindow({ width: 1920, height: 1080, useContentSize: true, show: false, webPreferences: { offscreen: true, backgroundThrottling: false } });
  window.webContents.setFrameRate(240);
  try {
    const query = new URLSearchParams({ w: width, h: height, fx: look ? 1 : 0, take: takeUrl, set: setUrl });
    await window.loadURL(`${origin}/index.html?${query}`);
    await window.webContents.executeJavaScript(`new Promise((resolve, reject) => (function poll(n) { window.report?.ready ? resolve() : n > 400 ? reject(new Error("the Stage did not start")) : setTimeout(() => poll(n + 1), 50); })(0))`);
    const errors = await window.webContents.executeJavaScript("report.errors");
    if (errors.length) throw new Error(errors.join("; "));
    return await window.webContents.executeJavaScript(`exportTake({ fps: ${Number(fps)}, name: ${JSON.stringify(name)} })`);
  } finally {
    window.destroy();
  }
}
