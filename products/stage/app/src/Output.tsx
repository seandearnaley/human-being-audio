// The picture. It is another origin in a frame, so nothing in a scene file can reach the app. The
// app hands it a port for the plugins' lines, and it reports back what each layer is showing.
import { useEffect, useImperativeHandle, useRef, type Ref } from "react";
import type { Showing } from "./stage.d";

export const OUTPUT = "stage://output";
export type OutputHandle = { line(line: string): void; audition(layer: number, note: number | null): void };

export function Output({ src, onShowing, ref }: { src: string; onShowing(showing: Showing): void; ref: Ref<OutputHandle> }) {
  const frame = useRef<HTMLIFrameElement>(null);
  const port = useRef<MessagePort | null>(null);
  const report = useRef(onShowing);
  report.current = onShowing;

  useImperativeHandle(ref, () => ({
    line: (line) => port.current?.postMessage(line),
    audition: (layer, note) => frame.current?.contentWindow?.postMessage({ type: "audition", layer, note }, OUTPUT),
  }), []);

  useEffect(() => {
    port.current = null;
    const listen = (event: MessageEvent) => {
      if (event.source !== frame.current?.contentWindow) return;
      if (event.data?.type === "port?" && frame.current?.contentWindow) {
        const channel = new MessageChannel();
        port.current = channel.port1;
        frame.current.contentWindow.postMessage({ type: "port" }, OUTPUT, [channel.port2]);
      }
      if (event.data?.type === "state") report.current(event.data.layers);
    };
    addEventListener("message", listen);
    return () => removeEventListener("message", listen);
  }, [src]);

  return <div className="stage"><iframe ref={frame} key={src} src={src} title="Output" className="screen" /></div>;
}
