// Publication-grade scientific canvas chart — dark Obsidian/Carbon theme.
// Renders line charts with grid, axis labels, tick marks, optional horizontal bands,
// and multiple overlay series. Mirrors egui_plot styling used in the Rust cockpit.

import React, { useEffect, useRef } from 'react';

export interface Series {
  label: string;
  x: number[];
  y: number[];
  color: string;
  width?: number;
  dash?: number[];
  marker?: 'circle' | 'none';
}

export interface Band {
  yMin: number;
  yMax: number;
  color: string; // typically semi-transparent
  label?: string;
}

export interface HLine {
  y: number;
  color: string;
  label?: string;
  dash?: number[];
  width?: number;
}

export interface ScientificChartProps {
  title?: string;
  xLabel: string;
  yLabel: string;
  series: Series[];
  bands?: Band[];
  hlines?: HLine[];
  invertY?: boolean;
  xMin?: number;
  xMax?: number;
  yMin?: number;
  yMax?: number;
  height?: number;
  width?: number;
  showLegend?: boolean;
  fontFamily?: string;
}

const THEME = {
  bg: '#111217',
  plotBg: '#18181C',
  axis: '#6B7280',
  grid: '#272932',
  tickText: '#9CA3AF',
  title: '#E5E7EB',
  label: '#D1D5DB',
};

function niceTicks(min: number, max: number, count = 6): number[] {
  if (min === max) return [min];
  const range = max - min;
  const rough = range / count;
  const pow10 = Math.pow(10, Math.floor(Math.log10(rough)));
  const normalized = rough / pow10;
  let step: number;
  if (normalized < 1.5) step = 1 * pow10;
  else if (normalized < 3) step = 2 * pow10;
  else if (normalized < 7) step = 5 * pow10;
  else step = 10 * pow10;
  const ticks: number[] = [];
  const start = Math.ceil(min / step) * step;
  for (let v = start; v <= max + step * 0.5; v += step) {
    ticks.push(Number(v.toFixed(10)));
  }
  return ticks;
}

function fmtTick(v: number): string {
  if (Math.abs(v) < 1e-9) return '0';
  const a = Math.abs(v);
  if (a >= 1000 || a < 0.01) return v.toExponential(1);
  if (a < 1) return v.toFixed(2);
  if (a < 10) return v.toFixed(2);
  return v.toFixed(1);
}

export const ScientificChart: React.FC<ScientificChartProps> = ({
  title,
  xLabel,
  yLabel,
  series,
  bands = [],
  hlines = [],
  invertY = false,
  xMin: xMinProp,
  xMax: xMaxProp,
  yMin: yMinProp,
  yMax: yMaxProp,
  height = 320,
  width,
  showLegend = true,
  fontFamily = "'JetBrains Mono', 'Inter', ui-monospace, system-ui, sans-serif",
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const wrapperRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    const wrap = wrapperRef.current;
    if (!canvas || !wrap) return;

    const draw = () => {
      const dpr = window.devicePixelRatio || 1;
      const w = (width ?? wrap.clientWidth);
      const h = height;
      canvas.width = Math.floor(w * dpr);
      canvas.height = Math.floor(h * dpr);
      canvas.style.width = `${w}px`;
      canvas.style.height = `${h}px`;
      const ctx = canvas.getContext('2d');
      if (!ctx) return;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

      // Margins
      const ml = 64, mr = 16, mt = title ? 36 : 16, mb = 42;
      const pw = w - ml - mr;
      const ph = h - mt - mb;

      // Background
      ctx.fillStyle = THEME.plotBg;
      ctx.fillRect(0, 0, w, h);

      // Determine data range
      let xMin = xMinProp ?? Infinity;
      let xMax = xMaxProp ?? -Infinity;
      let yMin = yMinProp ?? Infinity;
      let yMax = yMaxProp ?? -Infinity;
      if (xMinProp === undefined || xMaxProp === undefined) {
        for (const s of series) {
          for (const xv of s.x) { if (Number.isFinite(xv)) { xMin = Math.min(xMin, xv); xMax = Math.max(xMax, xv); } }
        }
      }
      if (yMinProp === undefined || yMaxProp === undefined) {
        for (const s of series) {
          for (const yv of s.y) { if (Number.isFinite(yv)) { yMin = Math.min(yMin, yv); yMax = Math.max(yMax, yv); } }
        }
        for (const b of bands) { yMin = Math.min(yMin, b.yMin); yMax = Math.max(yMax, b.yMax); }
        for (const hl of hlines) { yMin = Math.min(yMin, hl.y); yMax = Math.max(yMax, hl.y); }
      }
      if (!Number.isFinite(xMin)) { xMin = 0; xMax = 1; }
      if (!Number.isFinite(yMin)) { yMin = 0; yMax = 1; }
      if (xMin === xMax) { xMin -= 0.5; xMax += 0.5; }
      if (yMin === yMax) { yMin -= 0.5; yMax += 0.5; }
      const yPad = (yMax - yMin) * 0.08;
      yMin -= yPad; yMax += yPad;

      const xToPx = (x: number) => ml + ((x - xMin) / (xMax - xMin)) * pw;
      const yToPx = (y: number) => mt + ((invertY ? (yMax - y) : (y - yMin)) / (yMax - yMin)) * ph;

      // Plot area border
      ctx.strokeStyle = THEME.axis;
      ctx.lineWidth = 1;
      ctx.strokeRect(ml + 0.5, mt + 0.5, pw, ph);

      // Bands
      for (const b of bands) {
        const y1 = yToPx(Math.max(b.yMin, yMin));
        const y2 = yToPx(Math.min(b.yMax, yMax));
        ctx.fillStyle = b.color;
        ctx.fillRect(ml, Math.min(y1, y2), pw, Math.abs(y2 - y1));
      }

      // Grid + ticks
      ctx.font = `11px ${fontFamily}`;
      ctx.fillStyle = THEME.tickText;
      ctx.strokeStyle = THEME.grid;
      ctx.lineWidth = 1;
      const xTicks = niceTicks(xMin, xMax);
      const yTicks = niceTicks(yMin, yMax);
      ctx.beginPath();
      for (const xt of xTicks) {
        const xp = xToPx(xt);
        ctx.moveTo(xp, mt);
        ctx.lineTo(xp, mt + ph);
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';
        ctx.fillText(fmtTick(xt), xp, mt + ph + 6);
      }
      for (const yt of yTicks) {
        const yp = yToPx(yt);
        ctx.moveTo(ml, yp);
        ctx.lineTo(ml + pw, yp);
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        ctx.fillText(fmtTick(yt), ml - 6, yp);
      }
      ctx.stroke();

      // Axis labels
      ctx.fillStyle = THEME.label;
      ctx.font = `12px ${fontFamily}`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'bottom';
      ctx.fillText(xLabel, ml + pw / 2, h - 6);
      ctx.save();
      ctx.translate(14, mt + ph / 2);
      ctx.rotate(-Math.PI / 2);
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      ctx.fillText(yLabel, 0, 0);
      ctx.restore();

      // Title
      if (title) {
        ctx.fillStyle = THEME.title;
        ctx.font = `600 13px ${fontFamily}`;
        ctx.textAlign = 'left';
        ctx.textBaseline = 'top';
        ctx.fillText(title, ml, 8);
      }

      // Horizontal lines (e.g., y+=1, y+=30)
      for (const hl of hlines) {
        ctx.save();
        ctx.strokeStyle = hl.color;
        ctx.lineWidth = hl.width ?? 1;
        if (hl.dash) ctx.setLineDash(hl.dash);
        const yp = yToPx(hl.y);
        ctx.beginPath();
        ctx.moveTo(ml, yp);
        ctx.lineTo(ml + pw, yp);
        ctx.stroke();
        ctx.restore();
        if (hl.label) {
          ctx.fillStyle = hl.color;
          ctx.font = `10px ${fontFamily}`;
          ctx.textAlign = 'left';
          ctx.textBaseline = 'bottom';
          ctx.fillText(hl.label, ml + 4, yp - 2);
        }
      }

      // Series
      for (const s of series) {
        ctx.save();
        ctx.strokeStyle = s.color;
        ctx.lineWidth = s.width ?? 1.75;
        if (s.dash) ctx.setLineDash(s.dash);
        ctx.beginPath();
        let started = false;
        for (let i = 0; i < s.x.length; i++) {
          const xv = s.x[i];
          const yv = s.y[i];
          if (!Number.isFinite(xv) || !Number.isFinite(yv)) { started = false; continue; }
          const xp = xToPx(xv);
          const yp = yToPx(yv);
          if (!started) { ctx.moveTo(xp, yp); started = true; }
          else ctx.lineTo(xp, yp);
        }
        ctx.stroke();
        ctx.restore();
        if (s.marker === 'circle') {
          ctx.fillStyle = s.color;
          for (let i = 0; i < s.x.length; i++) {
            const xv = s.x[i], yv = s.y[i];
            if (!Number.isFinite(xv) || !Number.isFinite(yv)) continue;
            ctx.beginPath();
            ctx.arc(xToPx(xv), yToPx(yv), 2.5, 0, Math.PI * 2);
            ctx.fill();
          }
        }
      }

      // Legend
      if (showLegend && series.length > 0) {
        const lx = ml + 8;
        let ly = mt + 8;
        ctx.font = `11px ${fontFamily}`;
        const boxH = 14;
        const lineH = 16;
        const maxLabelW = Math.max(...series.map((s) => ctx.measureText(s.label).width));
        ctx.fillStyle = 'rgba(17,18,23,0.85)';
        ctx.fillRect(lx - 4, ly - 2, maxLabelW + 28, series.length * lineH + 4);
        ctx.strokeStyle = '#2A2D36';
        ctx.strokeRect(lx - 4 + 0.5, ly - 2 + 0.5, maxLabelW + 28, series.length * lineH + 4);
        for (let i = 0; i < series.length; i++) {
          const s = series[i];
          ctx.strokeStyle = s.color;
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.moveTo(lx, ly + i * lineH + boxH / 2);
          ctx.lineTo(lx + 18, ly + i * lineH + boxH / 2);
          ctx.stroke();
          ctx.fillStyle = s.color;
          ctx.textAlign = 'left';
          ctx.textBaseline = 'middle';
          ctx.fillText(s.label, lx + 24, ly + i * lineH + boxH / 2);
        }
      }
    };

    draw();
    const ro = new ResizeObserver(() => draw());
    ro.observe(wrap);
    return () => ro.disconnect();
  }, [series, bands, hlines, invertY, xMinProp, xMaxProp, yMinProp, yMaxProp, height, width, title, xLabel, yLabel, showLegend, fontFamily]);

  return (
    <div ref={wrapperRef} className="w-full rounded-lg overflow-hidden border border-zinc-800/60" style={{ background: THEME.bg }}>
      <canvas ref={canvasRef} />
    </div>
  );
};
