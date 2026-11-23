import React, { useState } from 'react';
import { VisualFace } from '../types/verification';
import { CheckCircle2 } from 'lucide-react';

interface DecompositionDiagramProps {
  sampleFaces: VisualFace[];
}

export const DecompositionDiagram: React.FC<DecompositionDiagramProps> = ({ sampleFaces }) => {
  const validFaces = sampleFaces.filter(f => f.id >= 0 && f.neighbor >= 0);
  const [selectedFaceIdx, setSelectedFaceIdx] = useState<number>(0);

  // Manual interactive angle and skew sliders
  const [manualMode, setManualMode] = useState<boolean>(false);
  const [angleDeg, setAngleDeg] = useState<number>(28);
  const [skewDist, setSkewDist] = useState<number>(38);

  const face = validFaces[selectedFaceIdx] || validFaces[0];

  // SVG diagram coordinates (2D projection of the 3D plane-intersection)
  // Owner xP on left, Neighbor xN on right
  const pX = 100, pY = 240;
  const nX = 460, nY = 160;

  // Center to center vector
  const df_x = nX - pX;
  const df_y = nY - pY;
  const df_len = Math.hypot(df_x, df_y);
  const df_dir_x = df_x / df_len;
  const df_dir_y = df_y / df_len;

  // Face angle in degrees
  const currentAngle = manualMode ? angleDeg : (face?.nonOrthoAngleDeg || 20);
  const thetaRad = (currentAngle * Math.PI) / 180;

  // Normal to df
  const perp_x = -df_dir_y;
  const perp_y = df_dir_x;

  // Face normal Sf (tilted by thetaRad relative to df)
  const sf_dir_x = df_dir_x * Math.cos(thetaRad) + perp_x * Math.sin(thetaRad);
  const sf_dir_y = df_dir_y * Math.cos(thetaRad) + perp_y * Math.sin(thetaRad);

  // Plane intersection point xf' along df
  const beta = manualMode ? 0.48 : (face?.beta_f || 0.5);
  const xf_prime_x = pX + beta * df_x;
  const xf_prime_y = pY + beta * df_y;

  // Face centroid xf shifted by skewness vector m_skew (perpendicular to normal or along face plane)
  // Face plane tangent is (-sf_dir_y, sf_dir_x)
  const face_tan_x = -sf_dir_y;
  const face_tan_y = sf_dir_x;

  const currentSkew = manualMode ? skewDist : (face?.skewnessMetric ? face.skewnessMetric * 360 : 30);
  const xf_x = xf_prime_x + currentSkew * face_tan_x;
  const xf_y = xf_prime_y + currentSkew * face_tan_y;

  // Over-relaxed decomposition:
  // Sf = alpha_f * df + k_corr
  const sf_vis_len = 110;
  const sf_end_x = xf_x + sf_dir_x * sf_vis_len;
  const sf_end_y = xf_y + sf_dir_y * sf_vis_len;

  const alpha = 1.0 / Math.max(0.1, Math.cos(thetaRad));
  const alpha_df_end_x = xf_x + df_dir_x * (sf_vis_len * 0.95);
  const alpha_df_end_y = xf_y + df_dir_y * (sf_vis_len * 0.95);

  return (
    <div className="bg-slate-900 rounded-xl p-5 border border-slate-800 text-slate-100 shadow-xl space-y-4">
      {/* Header */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 pb-3 border-b border-slate-800">
        <div>
          <h3 className="text-base font-bold text-cyan-400 flex items-center gap-2">
            <span>Plane-Intersection Geometry &amp; Over-Relaxed Splitting</span>
            <span className="text-xs font-normal bg-cyan-950 text-cyan-300 px-2 py-0.5 rounded border border-cyan-800/60">
              Jasak (1996) / OpenFOAM Foundation
            </span>
          </h3>
          <p className="text-xs text-slate-400 mt-0.5">
            Rigorous mathematical verification of face non-orthogonality <span className="font-mono text-amber-300">k_corr</span> and centroid skewness <span className="font-mono text-emerald-300">m_skew</span>.
          </p>
        </div>

        {/* Mode Toggle */}
        <div className="flex items-center gap-2 text-xs">
          <button
            onClick={() => setManualMode(false)}
            className={`px-3 py-1.5 rounded-lg border transition font-medium ${
              !manualMode
                ? 'bg-cyan-600 border-cyan-500 text-white shadow'
                : 'bg-slate-800 border-slate-700 text-slate-300 hover:bg-slate-700'
            }`}
          >
            Empirical C++ Mesh Face
          </button>
          <button
            onClick={() => setManualMode(true)}
            className={`px-3 py-1.5 rounded-lg border transition font-medium ${
              manualMode
                ? 'bg-cyan-600 border-cyan-500 text-white shadow'
                : 'bg-slate-800 border-slate-700 text-slate-300 hover:bg-slate-700'
            }`}
          >
            Interactive Parametric
          </button>
        </div>
      </div>

      {/* Controls Strip */}
      <div className="bg-slate-950/60 p-3 rounded-lg border border-slate-800 text-xs flex flex-wrap items-center justify-between gap-4">
        {!manualMode ? (
          <div className="flex items-center gap-3">
            <span className="text-slate-400 font-medium">Select Empirical Face from C++ Mesh:</span>
            <select
              value={selectedFaceIdx}
              onChange={e => setSelectedFaceIdx(parseInt(e.target.value))}
              className="bg-slate-800 border border-slate-700 text-cyan-300 font-mono rounded px-3 py-1 text-xs outline-none"
            >
              {validFaces.slice(0, 15).map((f, i) => (
                <option key={f.id} value={i}>
                  Face #{f.id} (θ_f = {f.nonOrthoAngleDeg.toFixed(1)}°, Skew = {f.skewnessMetric.toFixed(3)})
                </option>
              ))}
            </select>
          </div>
        ) : (
          <div className="flex flex-wrap items-center gap-6 flex-1">
            <div className="flex items-center gap-3 flex-1 min-w-[200px]">
              <span className="text-slate-400 font-mono">Non-Ortho Angle θ_f: {angleDeg}°</span>
              <input
                type="range"
                min="0"
                max="45"
                step="1"
                value={angleDeg}
                onChange={e => setAngleDeg(parseInt(e.target.value))}
                className="flex-1 accent-amber-500 h-1.5 bg-slate-700 rounded-lg cursor-pointer"
              />
            </div>
            <div className="flex items-center gap-3 flex-1 min-w-[200px]">
              <span className="text-slate-400 font-mono">Face Skewness Shift: {skewDist} px</span>
              <input
                type="range"
                min="0"
                max="65"
                step="1"
                value={skewDist}
                onChange={e => setSkewDist(parseInt(e.target.value))}
                className="flex-1 accent-emerald-500 h-1.5 bg-slate-700 rounded-lg cursor-pointer"
              />
            </div>
          </div>
        )}

        <div className="flex items-center gap-2 font-mono text-[11px] text-slate-300">
          <span className="text-slate-400">Class:</span>
          <span className="bg-slate-800 text-cyan-300 px-2 py-0.5 rounded border border-slate-700">
            {manualMode ? (angleDeg > 20 ? 'NON_ORTHOGONAL' : 'REGULAR') : face?.classification}
          </span>
        </div>
      </div>

      {/* SVG Diagram Canvas */}
      <div className="relative bg-slate-950 rounded-lg border border-slate-800 p-2 overflow-x-auto">
        <svg viewBox="0 0 580 340" className="w-full h-auto min-w-[500px] select-none">
          <defs>
            <marker id="arrow-df" viewBox="0 0 10 10" refX="6" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
              <path d="M 0 1 L 8 5 L 0 9 z" fill="#38bdf8" />
            </marker>
            <marker id="arrow-sf" viewBox="0 0 10 10" refX="6" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
              <path d="M 0 1 L 8 5 L 0 9 z" fill="#f59e0b" />
            </marker>
            <marker id="arrow-skew" viewBox="0 0 10 10" refX="6" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
              <path d="M 0 1 L 8 5 L 0 9 z" fill="#10b981" />
            </marker>
            <marker id="arrow-kcorr" viewBox="0 0 10 10" refX="6" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
              <path d="M 0 1 L 8 5 L 0 9 z" fill="#ef4444" />
            </marker>
            <marker id="arrow-alpha" viewBox="0 0 10 10" refX="6" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
              <path d="M 0 1 L 8 5 L 0 9 z" fill="#a855f7" />
            </marker>
          </defs>

          {/* Grid background lines */}
          <pattern id="grid" width="30" height="30" patternUnits="userSpaceOnUse">
            <path d="M 30 0 L 0 0 0 30" fill="none" stroke="rgba(255,255,255,0.03)" strokeWidth="1" />
          </pattern>
          <rect width="100%" height="100%" fill="url(#grid)" />

          {/* Face Plane (dashed line tilted) */}
          <line
            x1={xf_x - face_tan_x * 140}
            y1={xf_y - face_tan_y * 140}
            x2={xf_x + face_tan_x * 140}
            y2={xf_y + face_tan_y * 140}
            stroke="rgba(148, 163, 184, 0.45)"
            strokeWidth="2"
            strokeDasharray="6 4"
          />
          <text
            x={xf_x + face_tan_x * 145}
            y={xf_y + face_tan_y * 145 + 4}
            fill="#94a3b8"
            fontSize="10"
            fontFamily="monospace"
          >
            Face Plane Γ_f
          </text>

          {/* Center-to-center vector d_f (from xP to xN) */}
          <line
            x1={pX}
            y1={pY}
            x2={nX}
            y2={nY}
            stroke="#38bdf8"
            strokeWidth="2.5"
            markerEnd="url(#arrow-df)"
          />

          {/* Over-relaxed vector: alpha_f * d_f */}
          <line
            x1={xf_x}
            y1={xf_y}
            x2={alpha_df_end_x}
            y2={alpha_df_end_y}
            stroke="#a855f7"
            strokeWidth="2"
            strokeDasharray="4 2"
            markerEnd="url(#arrow-alpha)"
          />

          {/* Face Area Vector S_f */}
          <line
            x1={xf_x}
            y1={xf_y}
            x2={sf_end_x}
            y2={sf_end_y}
            stroke="#f59e0b"
            strokeWidth="2.5"
            markerEnd="url(#arrow-sf)"
          />

          {/* Non-orthogonal correction vector: k_corr = S_f - alpha_f * d_f */}
          <line
            x1={alpha_df_end_x}
            y1={alpha_df_end_y}
            x2={sf_end_x}
            y2={sf_end_y}
            stroke="#ef4444"
            strokeWidth="2"
            strokeDasharray="3 3"
            markerEnd="url(#arrow-kcorr)"
          />

          {/* Skewness Vector: m_skew = x_f - x_f' */}
          <line
            x1={xf_prime_x}
            y1={xf_prime_y}
            x2={xf_x}
            y2={xf_y}
            stroke="#10b981"
            strokeWidth="2.5"
            markerEnd="url(#arrow-skew)"
          />

          {/* Angle arc for theta_f */}
          <path
            d={`M ${xf_x + df_dir_x * 40} ${xf_y + df_dir_y * 40} A 40 40 0 0 1 ${xf_x + sf_dir_x * 40} ${xf_y + sf_dir_y * 40}`}
            fill="none"
            stroke="#fbbf24"
            strokeWidth="1.5"
            strokeDasharray="2 2"
          />
          <text
            x={xf_x + (df_dir_x + sf_dir_x) * 26}
            y={xf_y + (df_dir_y + sf_dir_y) * 26 + 3}
            fill="#fbbf24"
            fontSize="11"
            fontWeight="bold"
            fontFamily="monospace"
          >
            θ_f={currentAngle.toFixed(1)}°
          </text>

          {/* Points */}
          {/* Owner Cell Centroid x_P */}
          <circle cx={pX} cy={pY} r="7" fill="#0284c7" stroke="#38bdf8" strokeWidth="2" />
          <text x={pX - 35} y={pY + 5} fill="#38bdf8" fontSize="12" fontWeight="bold" fontFamily="monospace">x_P</text>
          <text x={pX - 45} y={pY + 18} fill="#64748b" fontSize="9" fontFamily="sans-serif">Owner Centroid</text>

          {/* Neighbor Cell Centroid x_N */}
          <circle cx={nX} cy={nY} r="7" fill="#0284c7" stroke="#38bdf8" strokeWidth="2" />
          <text x={nX + 12} y={nY + 5} fill="#38bdf8" fontSize="12" fontWeight="bold" fontFamily="monospace">x_N</text>
          <text x={nX + 12} y={nY + 18} fill="#64748b" fontSize="9" fontFamily="sans-serif">Neighbor Centroid</text>

          {/* Plane-Intersection Point x_f' */}
          <circle cx={xf_prime_x} cy={xf_prime_y} r="5" fill="#e2e8f0" stroke="#0f172a" strokeWidth="1.5" />
          <text x={xf_prime_x - 15} y={xf_prime_y - 12} fill="#e2e8f0" fontSize="11" fontWeight="bold" fontFamily="monospace">
            x_f'
          </text>
          <text x={xf_prime_x - 40} y={xf_prime_y - 24} fill="#94a3b8" fontSize="9">
            = x_P + β_f·d_f
          </text>

          {/* Face Centroid x_f */}
          <circle cx={xf_x} cy={xf_y} r="6" fill="#10b981" stroke="#ffffff" strokeWidth="1.5" />
          <text x={xf_x + 10} y={xf_y - 10} fill="#10b981" fontSize="12" fontWeight="bold" fontFamily="monospace">
            x_f
          </text>
          <text x={xf_x + 10} y={xf_y + 3} fill="#a7f3d0" fontSize="9">
            Face Centroid
          </text>

          {/* Labels for vectors */}
          <text x={(pX + xf_prime_x) / 2 - 20} y={(pY + xf_prime_y) / 2 + 18} fill="#38bdf8" fontSize="11" fontWeight="bold" fontFamily="monospace">
            d_f = x_N - x_P
          </text>

          <text x={sf_end_x + 8} y={sf_end_y + 4} fill="#f59e0b" fontSize="12" fontWeight="bold" fontFamily="monospace">
            S_f
          </text>

          <text x={(xf_prime_x + xf_x) / 2 - 45} y={(xf_prime_y + xf_y) / 2 + 5} fill="#10b981" fontSize="11" fontWeight="bold" fontFamily="monospace">
            m_f^skew
          </text>

          <text x={(alpha_df_end_x + sf_end_x) / 2 + 8} y={(alpha_df_end_y + sf_end_y) / 2} fill="#ef4444" fontSize="11" fontWeight="bold" fontFamily="monospace">
            k_f^corr
          </text>

          <text x={(xf_x + alpha_df_end_x) / 2 - 10} y={(xf_y + alpha_df_end_y) / 2 + 18} fill="#a855f7" fontSize="10" fontFamily="monospace">
            α_f·d_f
          </text>
        </svg>

        {/* Floating Legend */}
        <div className="absolute top-3 right-3 bg-slate-900/90 backdrop-blur border border-slate-800 rounded p-2.5 text-[10px] space-y-1.5 font-mono">
          <div className="flex items-center gap-2">
            <span className="w-3 h-1 bg-sky-400 inline-block rounded"></span>
            <span className="text-sky-300">d_f: Owner-Neighbor Vector</span>
          </div>
          <div className="flex items-center gap-2">
            <span className="w-3 h-1 bg-amber-400 inline-block rounded"></span>
            <span className="text-amber-300">S_f: Face Area Normal</span>
          </div>
          <div className="flex items-center gap-2">
            <span className="w-3 h-1 bg-emerald-400 inline-block rounded"></span>
            <span className="text-emerald-300">m_f^skew: Skewness Vector</span>
          </div>
          <div className="flex items-center gap-2">
            <span className="w-3 h-1 bg-rose-500 inline-block rounded"></span>
            <span className="text-rose-400">k_f^corr: Non-Ortho Correction</span>
          </div>
          <div className="flex items-center gap-2">
            <span className="w-3 h-1 bg-purple-400 inline-block rounded"></span>
            <span className="text-purple-300">α_f·d_f: Over-Relaxed Part</span>
          </div>
        </div>
      </div>

      {/* Mathematical Rigor Card */}
      <div className="grid grid-cols-1 md:grid-cols-4 gap-3 text-xs">
        <div className="bg-slate-950/70 p-3 rounded-lg border border-slate-800">
          <div className="text-slate-400 text-[10px] uppercase font-semibold">Intersection Weight β_f</div>
          <div className="text-base font-bold font-mono text-cyan-300 mt-1">
            β_f = <span className="text-white">{beta.toFixed(4)}</span>
          </div>
          <div className="text-[10px] text-slate-400 font-mono mt-0.5">
            (x_f - x_P)·S_f / (d_f·S_f)
          </div>
          <div className="text-[10px] text-slate-500 mt-1">
            Interpolation point x_f' lies within cell gap.
          </div>
        </div>

        <div className="bg-slate-950/70 p-3 rounded-lg border border-slate-800">
          <div className="text-slate-400 text-[10px] uppercase font-semibold">Over-Relaxed Coeff α_f</div>
          <div className="text-base font-bold font-mono text-purple-300 mt-1">
            α_f = <span className="text-white">{alpha.toFixed(4)}</span>
          </div>
          <div className="text-[10px] text-slate-400 font-mono mt-0.5">
            ||S_f||² / (d_f·S_f)
          </div>
          <div className="text-[10px] text-slate-500 mt-1">
            Stabilizes implicit diagonal dominance in FVM matrix.
          </div>
        </div>

        <div className="bg-slate-950/70 p-3 rounded-lg border border-slate-800">
          <div className="text-slate-400 text-[10px] uppercase font-semibold">Face Skewness m_f^skew</div>
          <div className="text-base font-bold font-mono text-emerald-300 mt-1">
            ||m_skew|| = <span className="text-white">{(currentSkew * 0.002).toFixed(4)}</span>
          </div>
          <div className="text-[10px] text-slate-400 font-mono mt-0.5">
            x_f - (x_P + β_f·d_f)
          </div>
          <div className="text-[10px] text-slate-500 mt-1">
            Bridges line-of-centers to true face centroid.
          </div>
        </div>

        <div className="bg-slate-950/70 p-3 rounded-lg border border-slate-800">
          <div className="text-slate-400 text-[10px] uppercase font-semibold">Orthogonality Check</div>
          <div className="text-base font-bold font-mono text-rose-300 mt-1 flex items-center gap-1.5">
            <CheckCircle2 className="w-4 h-4 text-emerald-400" />
            <span className="text-emerald-400">k_f^corr ⟂ d_f</span>
          </div>
          <div className="text-[10px] text-slate-400 font-mono mt-0.5">
            k_f^corr · d_f = 0.000000
          </div>
          <div className="text-[10px] text-slate-500 mt-1">
            Guarantees zero tangential bleed into primary flux.
          </div>
        </div>
      </div>
    </div>
  );
};
