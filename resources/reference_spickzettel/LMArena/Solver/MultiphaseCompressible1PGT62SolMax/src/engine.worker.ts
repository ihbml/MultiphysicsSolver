import { CHANNELS, METRICS, type Reply, type Request } from "./simulation";

type Engine = {
  _cb_create: (id: number, scheme: number, eos: number, cfl: number, hybrid: number, cavitation: number, quality: number) => number;
  _cb_advance: (t: number) => number;
  _cb_nx: () => number; _cb_ny: () => number; _cb_frames: () => number;
  _cb_lx: () => number; _cb_ly: () => number; _cb_end: () => number;
  _cb_metrics: () => number; _cb_fields: () => number;
  HEAPF32: Float32Array; HEAPF64: Float64Array;
};

const send = (reply: Reply, transfer: Transferable[] = []) => self.postMessage(reply, { transfer });

self.onmessage = async ({ data }: MessageEvent<Request>) => {
  const { settings: s, id, base } = data;
  try {
    const url = new URL("engine/cluster.js", base).href;
    const lib = await import(/* @vite-ignore */ url);
    const eng: Engine = await lib.default({ locateFile: (file: string) => new URL(`engine/${file}`, base).href });
    if (!eng._cb_create(id, s.scheme, s.eos, s.cfl, +s.hybrid, +s.cavitation, 0)) throw new Error("Initial states are not admissible for these settings.");
    const nx = eng._cb_nx(), ny = eng._cb_ny(), count = eng._cb_frames(), end = eng._cb_end();
    send({ type: "grid", data: { nx, ny, count, end, lx: eng._cb_lx(), ly: eng._cb_ly() } });
    for (let i = 0; i < count; i++) {
      if (!eng._cb_advance(end * i / (count - 1))) throw new Error("The solver could not find an admissible step. Try a lower CFL or another EOS.");
      const dm = eng._cb_metrics() >>> 3, df = eng._cb_fields() >>> 2;
      const metrics = eng.HEAPF64.slice(dm, dm + METRICS);
      const values = eng.HEAPF32.slice(df, df + CHANNELS * nx * ny);
      send({ type: "frame", index: i, metrics, values }, [metrics.buffer, values.buffer]);
    }
    send({ type: "done" });
  } catch (e) {
    send({ type: "error", error: e instanceof Error ? e.message : "Browser solver failed." });
  }
};