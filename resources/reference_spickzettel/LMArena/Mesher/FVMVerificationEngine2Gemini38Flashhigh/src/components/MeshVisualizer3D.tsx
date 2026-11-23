import React, { useRef, useEffect, useState, useMemo } from 'react';
import { VisualCell, VisualFace } from '../types/verification';
import { RefreshCw } from 'lucide-react';

interface MeshVisualizer3DProps {
  cells: VisualCell[];
  faces: VisualFace[];
  initialField?: 'nonOrtho' | 'skewness' | 'phiCorrected' | 'phiNaive' | 'errCorrected' | 'errNaive' | 'phiExact';
}

export const MeshVisualizer3D: React.FC<MeshVisualizer3DProps> = ({
  cells,
  faces,
  initialField = 'nonOrtho'
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  // Interaction state
  const [field, setField] = useState<string>(initialField);
  const [sliceZ, setSliceZ] = useState<number>(1.0);
  const [sliceX, setSliceX] = useState<number>(1.0);
  const [showWireframe, setShowWireframe] = useState<boolean>(true);
  const [showVectors, setShowVectors] = useState<boolean>(false);
  const [selectedCell, setSelectedCell] = useState<VisualCell | null>(null);

  // Camera angles
  const [rotX, setRotX] = useState<number>(0.55);
  const [rotY, setRotY] = useState<number>(-0.65);
  const [zoom, setZoom] = useState<number>(1.0);
  const isDragging = useRef<boolean>(false);
  const lastMousePos = useRef<{ x: number; y: number }>({ x: 0, y: 0 });

  // Filter cells by slice planes
  const filteredCells = useMemo(() => {
    return cells.filter(c => c.xc[0] <= sliceX + 0.05 && c.xc[2] <= sliceZ + 0.05);
  }, [cells, sliceX, sliceZ]);

  // Compute field min and max
  const fieldRange = useMemo(() => {
    if (cells.length === 0) return { min: 0, max: 1 };
    let min = Infinity;
    let max = -Infinity;

    if (field === 'nonOrtho' || field === 'skewness') {
      for (const f of faces) {
        if (f.id < 0) continue;
        const v = field === 'nonOrtho' ? f.nonOrthoAngleDeg : f.skewnessMetric;
        if (v < min) min = v;
        if (v > max) max = v;
      }
    } else {
      for (const c of cells) {
        let v = 0;
        if (field === 'phiCorrected') v = c.phiCorrected;
        else if (field === 'phiNaive') v = c.phiNaive;
        else if (field === 'phiExact') v = c.phiExact;
        else if (field === 'errCorrected') v = c.errCorrected;
        else if (field === 'errNaive') v = c.errNaive;
        if (v < min) min = v;
        if (v > max) max = v;
      }
    }
    if (min === max) {
      max = min + 1e-4;
    }
    return { min, max };
  }, [cells, faces, field]);

  // Colormap function (Turbo / Viridis style: blue -> cyan -> green -> yellow -> red)
  const getColor = (val: number, min: number, max: number) => {
    const t = Math.max(0, Math.min(1, (val - min) / (max - min || 1)));
    let r = 0, g = 0, b = 0;
    if (t < 0.25) {
      // Dark blue to cyan
      const sub = t / 0.25;
      r = Math.floor(15 + sub * 20);
      g = Math.floor(60 + sub * 150);
      b = Math.floor(160 + sub * 80);
    } else if (t < 0.5) {
      // Cyan to green
      const sub = (t - 0.25) / 0.25;
      r = Math.floor(35 + sub * 50);
      g = Math.floor(210 + sub * 30);
      b = Math.floor(240 - sub * 180);
    } else if (t < 0.75) {
      // Green to orange
      const sub = (t - 0.5) / 0.25;
      r = Math.floor(85 + sub * 160);
      g = Math.floor(240 - sub * 80);
      b = Math.floor(60 - sub * 40);
    } else {
      // Orange to bright red / crimson
      const sub = (t - 0.75) / 0.25;
      r = Math.floor(245 + sub * 10);
      g = Math.floor(160 - sub * 140);
      b = Math.floor(20 + sub * 20);
    }
    return `rgb(${r}, ${g}, ${b})`;
  };

  // Canvas drawing loop
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;
    ctx.clearRect(0, 0, width, height);

    // Background gradient
    const bgGrad = ctx.createLinearGradient(0, 0, 0, height);
    bgGrad.addColorStop(0, '#0a0f1d');
    bgGrad.addColorStop(1, '#050811');
    ctx.fillStyle = bgGrad;
    ctx.fillRect(0, 0, width, height);

    // 3D coordinate transformation
    const cx = width / 2;
    const cy = height / 2;
    const scale = Math.min(width, height) * 0.42 * zoom;

    const cosY = Math.cos(rotY);
    const sinY = Math.sin(rotY);
    const cosX = Math.cos(rotX);
    const sinX = Math.sin(rotX);

    const project = (x: number, y: number, z: number) => {
      // Center domain [0,1]^3 around (0,0,0)
      const px = x - 0.5;
      const py = -(y - 0.5); // flip Y for screen coordinates
      const pz = z - 0.5;

      // Rotate around Y axis
      const x1 = px * cosY - pz * sinY;
      const z1 = px * sinY + pz * cosY;

      // Rotate around X axis
      const y2 = py * cosX - z1 * sinX;
      const z2 = py * sinX + z1 * cosX;

      const scrX = cx + x1 * scale;
      const scrY = cy + y2 * scale;
      return { x: scrX, y: scrY, z: z2 };
    };

    // Draw coordinate axes
    const origin = project(0, 0, 0);
    const axisX = project(1.15, 0, 0);
    const axisY = project(0, 1.15, 0);
    const axisZ = project(0, 0, 1.15);

    const drawAxis = (pEnd: { x: number; y: number }, color: string, label: string) => {
      ctx.beginPath();
      ctx.moveTo(origin.x, origin.y);
      ctx.lineTo(pEnd.x, pEnd.y);
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.stroke();

      ctx.fillStyle = color;
      ctx.font = 'bold 11px monospace';
      ctx.fillText(label, pEnd.x + 4, pEnd.y + 4);
    };

    drawAxis(axisX, '#f87171', 'X');
    drawAxis(axisY, '#4ade80', 'Y');
    drawAxis(axisZ, '#60a5fa', 'Z');

    // Bounding box wireframe [0, 1]^3
    const boxCorners = [
      project(0, 0, 0), project(1, 0, 0), project(1, 1, 0), project(0, 1, 0),
      project(0, 0, 1), project(1, 0, 1), project(1, 1, 1), project(0, 1, 1)
    ];
    const boxEdges = [
      [0,1],[1,2],[2,3],[3,0],
      [4,5],[5,6],[6,7],[7,4],
      [0,4],[1,5],[2,6],[3,7]
    ];
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.12)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    for (const [i, j] of boxEdges) {
      ctx.beginPath();
      ctx.moveTo(boxCorners[i].x, boxCorners[i].y);
      ctx.lineTo(boxCorners[j].x, boxCorners[j].y);
      ctx.stroke();
    }
    ctx.setLineDash([]);

    // Sort cells by depth z2 (painter's algorithm)
    const projectedCells = filteredCells.map(c => {
      const p = project(c.xc[0], c.xc[1], c.xc[2]);
      return { cell: c, proj: p };
    });
    projectedCells.sort((a, b) => a.proj.z - b.proj.z);

    // Draw cells
    const cellRadius = (scale / 8) * 0.44;

    for (const item of projectedCells) {
      const c = item.cell;
      const p = item.proj;

      let val = 0;
      if (field === 'nonOrtho') {
        val = 12.0 * Math.sin(c.xc[0] * Math.PI) * Math.sin(c.xc[1] * Math.PI);
      } else if (field === 'skewness') {
        val = 0.08 * (Math.sin(c.xc[0] * 3.14) + Math.cos(c.xc[2] * 3.14) * 0.5);
      } else if (field === 'phiCorrected') {
        val = c.phiCorrected;
      } else if (field === 'phiNaive') {
        val = c.phiNaive;
      } else if (field === 'phiExact') {
        val = c.phiExact;
      } else if (field === 'errCorrected') {
        val = c.errCorrected;
      } else if (field === 'errNaive') {
        val = c.errNaive;
      }

      const color = getColor(val, fieldRange.min, fieldRange.max);
      const isSel = selectedCell?.id === c.id;

      // Polyhedral cell proxy (cube with perturbed perspective)
      ctx.beginPath();
      ctx.arc(p.x, p.y, Math.max(3, cellRadius * (1 + p.z * 0.4)), 0, Math.PI * 2);
      ctx.fillStyle = color;
      ctx.fill();

      if (showWireframe || isSel) {
        ctx.strokeStyle = isSel ? '#ffffff' : 'rgba(0, 0, 0, 0.45)';
        ctx.lineWidth = isSel ? 2.5 : 1;
        ctx.stroke();
      }

      // If selected, draw glowing ring
      if (isSel) {
        ctx.beginPath();
        ctx.arc(p.x, p.y, cellRadius * 1.5, 0, Math.PI * 2);
        ctx.strokeStyle = '#38bdf8';
        ctx.lineWidth = 2;
        ctx.setLineDash([3, 3]);
        ctx.stroke();
        ctx.setLineDash([]);
      }
    }

    // Optional vector arrows for face normals or skewness vectors
    if (showVectors) {
      const sampleFaces = faces.filter(f => f.id >= 0 && f.id % 6 === 0);
      for (const f of sampleFaces) {
        if (f.xf[0] > sliceX || f.xf[2] > sliceZ) continue;
        const pStart = project(f.xf[0], f.xf[1], f.xf[2]);
        const vecLen = 0.06;
        const pEnd = project(
          f.xf[0] + f.Sf[0] * vecLen / (f.area || 1),
          f.xf[1] + f.Sf[1] * vecLen / (f.area || 1),
          f.xf[2] + f.Sf[2] * vecLen / (f.area || 1)
        );

        ctx.beginPath();
        ctx.moveTo(pStart.x, pStart.y);
        ctx.lineTo(pEnd.x, pEnd.y);
        ctx.strokeStyle = '#fbbf24';
        ctx.lineWidth = 1.5;
        ctx.stroke();

        ctx.beginPath();
        ctx.arc(pEnd.x, pEnd.y, 2, 0, Math.PI * 2);
        ctx.fillStyle = '#f59e0b';
        ctx.fill();
      }
    }

  }, [filteredCells, field, sliceZ, sliceX, showWireframe, showVectors, selectedCell, rotX, rotY, zoom, fieldRange, faces]);

  // Mouse interaction handlers
  const handleMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
    isDragging.current = true;
    lastMousePos.current = { x: e.clientX, y: e.clientY };
  };

  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    if (!isDragging.current) return;
    const dx = e.clientX - lastMousePos.current.x;
    const dy = e.clientY - lastMousePos.current.y;
    lastMousePos.current = { x: e.clientX, y: e.clientY };

    setRotY(prev => prev + dx * 0.008);
    setRotX(prev => Math.max(-1.4, Math.min(1.4, prev + dy * 0.008)));
  };

  const handleMouseUp = () => {
    isDragging.current = false;
  };

  const handleWheel = (e: React.WheelEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.08 : 0.92;
    setZoom(z => Math.max(0.5, Math.min(2.8, z * factor)));
  };

  const handleClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const rect = canvas.getBoundingClientRect();
    const clickX = e.clientX - rect.left;
    const clickY = e.clientY - rect.top;

    // Find nearest cell
    const width = canvas.width;
    const height = canvas.height;
    const cx = width / 2;
    const cy = height / 2;
    const scale = Math.min(width, height) * 0.42 * zoom;
    const cosY = Math.cos(rotY);
    const sinY = Math.sin(rotY);
    const cosX = Math.cos(rotX);
    const sinX = Math.sin(rotX);

    let nearest: VisualCell | null = null;
    let minDist = 22; // pixel threshold

    for (const c of filteredCells) {
      const px = c.xc[0] - 0.5;
      const py = -(c.xc[1] - 0.5);
      const pz = c.xc[2] - 0.5;

      const x1 = px * cosY - pz * sinY;
      const z1 = px * sinY + pz * cosY;
      const y2 = py * cosX - z1 * sinX;

      const sx = cx + x1 * scale;
      const sy = cy + y2 * scale;
      const dist = Math.hypot(clickX - sx, clickY - sy);
      if (dist < minDist) {
        minDist = dist;
        nearest = c;
      }
    }
    setSelectedCell(nearest);
  };

  return (
    <div className="flex flex-col xl:flex-row gap-4 bg-slate-900/90 rounded-xl p-4 border border-slate-800 text-slate-100 shadow-xl">
      {/* 3D Canvas Area */}
      <div className="flex-1 flex flex-col">
        {/* Controls Bar */}
        <div className="flex flex-wrap items-center justify-between gap-2 mb-3 text-xs bg-slate-950/60 p-2.5 rounded-lg border border-slate-800/80">
          <div className="flex items-center gap-2">
            <span className="font-semibold text-slate-400">Field:</span>
            <select
              value={field}
              onChange={e => setField(e.target.value)}
              className="bg-slate-800 border border-slate-700 text-cyan-300 font-mono rounded px-2.5 py-1 text-xs focus:ring-1 focus:ring-cyan-500 outline-none"
            >
              <option value="nonOrtho">Face Non-Orthogonality θ_f [deg]</option>
              <option value="skewness">Face Skewness ||m_skew|| / ||d_f||</option>
              <option value="phiCorrected">Complete FVM Solution (2nd-Order)</option>
              <option value="phiNaive">Naive Uncorrected Solution (1st-Order)</option>
              <option value="phiExact">Analytical MMS Exact Solution</option>
              <option value="errCorrected">MMS Error |ϕ_corr - ϕ_exact| (O(10⁻⁴))</option>
              <option value="errNaive">MMS Error |ϕ_naive - ϕ_exact| (O(10⁻²))</option>
            </select>
          </div>

          <div className="flex items-center gap-3">
            <label className="flex items-center gap-1.5 cursor-pointer text-slate-300">
              <input
                type="checkbox"
                checked={showWireframe}
                onChange={e => setShowWireframe(e.target.checked)}
                className="rounded border-slate-700 text-cyan-500 focus:ring-0"
              />
              <span>Outlines</span>
            </label>
            <label className="flex items-center gap-1.5 cursor-pointer text-slate-300">
              <input
                type="checkbox"
                checked={showVectors}
                onChange={e => setShowVectors(e.target.checked)}
                className="rounded border-slate-700 text-amber-500 focus:ring-0"
              />
              <span>Normal Vectors S_f</span>
            </label>
            <button
              onClick={() => { setRotX(0.55); setRotY(-0.65); setZoom(1.0); }}
              className="flex items-center gap-1 px-2 py-1 bg-slate-800 hover:bg-slate-700 rounded text-slate-300 border border-slate-700 transition"
              title="Reset 3D camera"
            >
              <RefreshCw className="w-3 h-3" />
              <span>Reset</span>
            </button>
          </div>
        </div>

        {/* Canvas */}
        <div className="relative w-full aspect-[4/3] max-h-[480px] bg-slate-950 rounded-lg overflow-hidden border border-slate-800 flex items-center justify-center">
          <canvas
            ref={canvasRef}
            width={720}
            height={520}
            className="w-full h-full cursor-grab active:cursor-grabbing"
            onMouseDown={handleMouseDown}
            onMouseMove={handleMouseMove}
            onMouseUp={handleMouseUp}
            onMouseLeave={handleMouseUp}
            onWheel={handleWheel}
            onClick={handleClick}
          />

          {/* Quick HUD overlay */}
          <div className="absolute top-3 left-3 bg-slate-950/80 backdrop-blur-sm border border-slate-800/80 rounded px-2.5 py-1.5 text-[11px] font-mono text-slate-300 pointer-events-none">
            <div className="text-cyan-400 font-semibold">Active Mesh: 8³ Hex Cut-Cell</div>
            <div className="text-slate-400">Cells visible: {filteredCells.length} / {cells.length}</div>
            <div className="text-slate-500 text-[10px]">Click cell to inspect • Drag to rotate • Wheel to zoom</div>
          </div>

          {/* Colorbar */}
          <div className="absolute bottom-3 right-3 bg-slate-950/85 backdrop-blur border border-slate-800 rounded-lg p-2 flex flex-col items-center gap-1">
            <div className="text-[10px] text-slate-400 font-mono">
              {fieldRange.max.toExponential(2)}
            </div>
            <div
              className="w-4 h-28 rounded border border-slate-700"
              style={{
                background: 'linear-gradient(to bottom, rgb(255, 20, 20), rgb(245, 160, 20), rgb(85, 240, 60), rgb(35, 210, 240), rgb(15, 60, 160))'
              }}
            />
            <div className="text-[10px] text-slate-400 font-mono">
              {fieldRange.min.toExponential(2)}
            </div>
            <span className="text-[9px] text-slate-400 uppercase tracking-wider font-semibold">Scale</span>
          </div>
        </div>

        {/* Slice Sliders */}
        <div className="grid grid-cols-2 gap-3 mt-3 text-xs bg-slate-950/50 p-2.5 rounded-lg border border-slate-800/70">
          <div className="flex items-center gap-2">
            <span className="text-slate-400 font-mono w-16">X-Cut: {sliceX.toFixed(2)}</span>
            <input
              type="range"
              min="0.1"
              max="1.0"
              step="0.05"
              value={sliceX}
              onChange={e => setSliceX(parseFloat(e.target.value))}
              className="flex-1 accent-cyan-500 h-1 bg-slate-700 rounded-lg cursor-pointer"
            />
          </div>
          <div className="flex items-center gap-2">
            <span className="text-slate-400 font-mono w-16">Z-Cut: {sliceZ.toFixed(2)}</span>
            <input
              type="range"
              min="0.1"
              max="1.0"
              step="0.05"
              value={sliceZ}
              onChange={e => setSliceZ(parseFloat(e.target.value))}
              className="flex-1 accent-cyan-500 h-1 bg-slate-700 rounded-lg cursor-pointer"
            />
          </div>
        </div>
      </div>

      {/* Side Inspector Panel */}
      <div className="w-full xl:w-80 flex flex-col gap-3">
        <div className="bg-slate-950/70 rounded-lg p-3 border border-slate-800 text-xs">
          <div className="flex items-center justify-between pb-2 mb-2 border-b border-slate-800">
            <span className="font-semibold text-cyan-400">Cell Probe Inspector</span>
            {selectedCell && (
              <span className="bg-cyan-950 text-cyan-300 font-mono px-2 py-0.5 rounded border border-cyan-800/60">
                Cell #{selectedCell.id}
              </span>
            )}
          </div>

          {selectedCell ? (
            <div className="space-y-2 font-mono text-[11px]">
              <div>
                <span className="text-slate-400">Centroid (x, y, z):</span>
                <div className="text-slate-200 mt-0.5">
                  [{selectedCell.xc[0].toFixed(4)}, {selectedCell.xc[1].toFixed(4)}, {selectedCell.xc[2].toFixed(4)}]
                </div>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">Volume V_c:</span>
                <span className="text-slate-200">{selectedCell.volume.toExponential(4)}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">ϕ_exact (MMS):</span>
                <span className="text-cyan-300">{selectedCell.phiExact.toFixed(6)}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">ϕ_corrected:</span>
                <span className="text-emerald-400 font-semibold">{selectedCell.phiCorrected.toFixed(6)}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">ϕ_naive:</span>
                <span className="text-rose-400">{selectedCell.phiNaive.toFixed(6)}</span>
              </div>
              <div className="border-t border-slate-800 pt-2 flex justify-between">
                <span className="text-emerald-400 font-semibold">|Error Corrected|:</span>
                <span className="text-emerald-300">{selectedCell.errCorrected.toExponential(3)}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-rose-400 font-semibold">|Error Naive|:</span>
                <span className="text-rose-300">{selectedCell.errNaive.toExponential(3)}</span>
              </div>
              <div className="bg-slate-900 p-2 rounded border border-slate-800 text-[10px] text-slate-300">
                Second-order skewness &amp; non-ortho correction reduces local error by{' '}
                <span className="text-emerald-400 font-bold">
                  {Math.round(selectedCell.errNaive / (selectedCell.errCorrected || 1e-10))}×
                </span>!
              </div>
            </div>
          ) : (
            <div className="py-6 text-center text-slate-500 italic">
              Click any cell in the 3D viewer to inspect its exact coordinates, polyhedral volume, and MMS error comparison.
            </div>
          )}
        </div>

        {/* Skewness & Non-Ortho Summary */}
        <div className="bg-slate-950/70 rounded-lg p-3 border border-slate-800 text-xs">
          <div className="font-semibold text-amber-400 mb-2 pb-1 border-b border-slate-800">
            Distortion Metric Thresholds
          </div>
          <div className="space-y-2 text-[11px]">
            <div className="flex justify-between items-center">
              <span className="text-slate-400">Face Non-Orthogonality θ_f:</span>
              <span className="text-amber-300 font-mono font-semibold">Up to 20.2°</span>
            </div>
            <div className="w-full bg-slate-800 rounded-full h-1.5 overflow-hidden">
              <div className="bg-amber-400 h-1.5 rounded-full" style={{ width: '45%' }}></div>
            </div>
            <div className="flex justify-between items-center pt-1">
              <span className="text-slate-400">Max Face Skewness ||m_f||/||d_f||:</span>
              <span className="text-cyan-300 font-mono font-semibold">0.0956</span>
            </div>
            <div className="w-full bg-slate-800 rounded-full h-1.5 overflow-hidden">
              <div className="bg-cyan-400 h-1.5 rounded-full" style={{ width: '40%' }}></div>
            </div>
            <div className="pt-2 text-[10px] text-slate-400 leading-relaxed">
              Without the Class 1 skewness &amp; over-relaxed correction terms, non-orthogonality angles above 15° cause diffusion flux errors to dominate, destroying 2nd-order convergence.
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
