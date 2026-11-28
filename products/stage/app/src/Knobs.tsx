// Eight knobs a layer. They are the plugin's eight automatable parameters on that layer's track,
// and they take the names of whatever scene the layer is showing.
export function Knobs({ title, scene, names, values }: { title: string; scene: string; names: string[]; values: number[] }) {
  return (
    <div className="knobs">
      <div className="knobs-of"><strong>{title}</strong><span>{scene}</span></div>
      {Array.from({ length: 8 }, (_, i) => {
        const value = values[i];
        return (
          <div key={i} className={names[i] ? "knob" : "knob idle"}>
            <div><span>{names[i] ?? ""}</span><code>{names[i] && value !== undefined ? value.toFixed(2) : ""}</code></div>
            <div className="track"><div style={{ width: `${names[i] && value !== undefined ? value * 100 : 0}%` }} /></div>
          </div>
        );
      })}
    </div>
  );
}
