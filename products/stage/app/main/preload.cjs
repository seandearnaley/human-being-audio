// The app page's only door to the main process. The output frame, where scene files run, gets none
// of this: Electron gives a preload to the top page only.
const { contextBridge, ipcRenderer } = require("electron");

const CALLS = ["set:get", "set:list", "set:open", "set:new", "set:rename", "set:assign", "set:choose", "library:list", "agent:make", "agent:discard", "take:record", "take:finish", "take:list", "take:export", "reveal"];
const listen = (channel) => (handler) => {
  const listener = (_event, value) => handler(value);
  ipcRenderer.on(channel, listener);
  return () => ipcRenderer.off(channel, listener);
};

contextBridge.exposeInMainWorld("stage", {
  call: (name, payload) => (CALLS.includes(name) ? ipcRenderer.invoke(name, payload) : Promise.reject(new Error(`unknown call ${name}`))),
  onLine: listen("line"),
  onAgentEvent: listen("agent:event"),
});
