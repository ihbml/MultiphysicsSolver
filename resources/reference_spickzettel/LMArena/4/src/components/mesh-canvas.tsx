"use client";

import { useEffect, useRef } from "react";

import type { MeshGeometry } from "@/lib/types";

const STOPS: [number, number, number][] = [
  [11, 22, 32],
  [16, 62, 92],
  [18, 130, 142],
  [114, 218, 160],
  [232, 248, 126],
];

function colorFor(t: number): [number, number, number] {
  const clamped = Math.max(0, Math.min(1, Number.isFinite(t) ? t : 0));
  const scaled = clamped * (STOPS.length - 1);
  const index = Math.min(STOPS.length - 2, Math.floor(scaled));
  const f = scaled - index;
  const a = STOPS[index];
  const b = STOPS[index + 1];
  return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
}

export function MeshCanvas({
  geometry,
  domain,
  values,
  range,
  label,
  showEdges = true,
}: {
  geometry: MeshGeometry | null;
  domain: { xmin: number; xmax: number; ymin: number; ymax: number };
  values: number[] | null;
  range: [number, number];
  label: string;
  showEdges?: boolean;
}) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const wrapRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    const wrap = wrapRef.current;
    if (!canvas || !wrap) return;

    const draw = () => {
      const ctx = canvas.getContext("2d");
      if (!ctx) return;
      const dpr = Math.min(2, window.devicePixelRatio || 1);
      const width = wrap.clientWidth || 720;
      const aspect = (domain.ymax - domain.ymin) / Math.max(1e-9, domain.xmax - domain.xmin);
      const height = Math.max(240, Math.min(560, width * aspect));
      canvas.width = Math.round(width * dpr);
      canvas.height = Math.round(height * dpr);
      canvas.style.width = `${width}px`;
      canvas.style.height = `${height}px`;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, width, height);

      const pad = 14;
      const scale = Math.min((width - 2 * pad) / (domain.xmax - domain.xmin), (height - 2 * pad) / (domain.ymax - domain.ymin));
      const ox = pad + ((width - 2 * pad) - scale * (domain.xmax - domain.xmin)) / 2;
      const oy = pad + ((height - 2 * pad) - scale * (domain.ymax - domain.ymin)) / 2;
      const px = (x: number) => ox + (x - domain.xmin) * scale;
      const py = (y: number) => height - oy - (y - domain.ymin) * scale;

      ctx.fillStyle = "#060a0c";
      ctx.fillRect(0, 0, width, height);

      if (!geometry) {
        ctx.strokeStyle = "#1d2b31";
        ctx.setLineDash([4, 4]);
        ctx.strokeRect(px(domain.xmin), py(domain.ymax), (domain.xmax - domain.xmin) * scale, (domain.ymax - domain.ymin) * scale);
        ctx.setLineDash([]);
        ctx.fillStyle = "#5f777a";
        ctx.font = "12px ui-monospace, Menlo, monospace";
        ctx.fillText("no mesh loaded", width / 2 - 52, height / 2);
        return;
      }

      const { cells, faces, verts, graph } = geometry;
      const [lo, hi] = range;
      const span = Math.abs(hi - lo) < 1e-12 ? 1 : hi - lo;

      ctx.lineJoin = "round";
      for (let i = 0; i < cells.length; i += 1) {
        const cell = cells[i];
        const value = values ? values[i] : cell[3];
        const t = value === undefined ? 0 : (value - lo) / span;
        const [r, g, b] = colorFor(t);
        ctx.fillStyle = `rgb(${r | 0},${g | 0},${b | 0})`;
        const cx = px(cell[0]);
        const cy = py(cell[1]);
        for (let k = graph.offsets[i]; k < graph.offsets[i + 1]; k += 1) {
          const face = faces[graph.indices[k]];
          const v0 = face[0] * 2;
          const v1 = face[1] * 2;
          ctx.beginPath();
          ctx.moveTo(cx, cy);
          ctx.lineTo(px(verts[v0]), py(verts[v0 + 1]));
          ctx.lineTo(px(verts[v1]), py(verts[v1 + 1]));
          ctx.closePath();
          ctx.fill();
        }
      }

      if (showEdges) {
        ctx.lineWidth = 0.5;
        ctx.strokeStyle = "rgba(4, 8, 10, 0.62)";
        ctx.beginPath();
        for (const face of faces) {
          const v0 = face[0] * 2;
          const v1 = face[1] * 2;
          ctx.moveTo(px(verts[v0]), py(verts[v0 + 1]));
          ctx.lineTo(px(verts[v1]), py(verts[v1 + 1]));
        }
        ctx.stroke();
      }

      // solid / box boundary
      ctx.lineWidth = 1.2;
      ctx.strokeStyle = "rgba(120, 220, 205, 0.55)";
      ctx.beginPath();
      for (const face of faces) {
        if (face[3] >= 0) continue;
        const v0 = face[0] * 2;
        const v1 = face[1] * 2;
        ctx.moveTo(px(verts[v0]), py(verts[v0 + 1]));
        ctx.lineTo(px(verts[v1]), py(verts[v1 + 1]));
      }
      ctx.stroke();

      // obstacle walls highlighted
      ctx.lineWidth = 1.8;
      ctx.strokeStyle = "rgba(255, 181, 69, 0.95)";
      ctx.beginPath();
      for (const face of faces) {
        if (face[3] >= 0 || face[7] !== 2) continue;
        const v0 = face[0] * 2;
        const v1 = face[1] * 2;
        ctx.moveTo(px(verts[v0]), py(verts[v0 + 1]));
        ctx.lineTo(px(verts[v1]), py(verts[v1 + 1]));
      }
      ctx.stroke();

      ctx.fillStyle = "rgba(230, 240, 240, 0.55)";
      ctx.font = "10px ui-monospace, Menlo, monospace";
      ctx.fillText(label, pad + 2, height - 6);
      ctx.fillText(`x: ${domain.xmin.toFixed(2)} → ${domain.xmax.toFixed(2)}`, width - 190, height - 6);
    };

    draw();
    const observer = new ResizeObserver(draw);
    observer.observe(wrap);
    return () => observer.disconnect();
  }, [geometry, domain, values, range, label, showEdges]);

  return (
    <div ref={wrapRef} className="w-full overflow-hidden rounded-xl border border-[#1d2b31] bg-[#060a0c]">
      <canvas ref={canvasRef} className="block w-full" />
    </div>
  );
}

export function ColorLegend({ range, label, unit }: { range: [number, number]; label: string; unit?: string }) {
  const gradient = `linear-gradient(90deg, rgb(${STOPS.map((s) => s.join(",")).join("), rgb(")}))`;
  return (
    <div className="flex items-center gap-3">
      <span className="label whitespace-nowrap">{label}</span>
      <span className="mono text-[11px] text-[#8aa4a5]">{range[0].toPrecision(3)}</span>
      <div className="h-2 flex-1 rounded-full border border-[#1d2b31]" style={{ background: gradient }} />
      <span className="mono text-[11px] text-[#8aa4a5]">
        {range[1].toPrecision(3)}
        {unit ? ` ${unit}` : ""}
      </span>
    </div>
  );
}
