// SI-checked Custom Field Calculator — React port of the Rust rhai AST dimension propagator.
// Shows dimension superscript errors and suggestions, plus live numeric preview.

import React, { useMemo, useState } from 'react';
import { analyzeAssignment } from '../field-parser';
import { dimToHuman } from '../si-units';
import { FIELD_VARS } from '../cfd-data';

interface DerivedField {
  name: string;
  expr: string;
  dim: string;
  dimVec: import('../types').DimVec;
}

export const FieldCalculator: React.FC<{
  onAdd?: (f: DerivedField) => void;
}> = ({ onAdd }) => {
  const [src, setSrc] = useState<string>(
    'p_total = p + 0.5 * rho * dot(U, U)'
  );
  const [derived, setDerived] = useState<DerivedField[]>([]);

  const analysis = useMemo(() => {
    if (!src.trim()) return null;
    return analyzeAssignment(src);
  }, [src]);

  // Sample values (in SI) for a transonic test point
  const sampleVals = useMemo(() => ({
    p: 101325 * 0.75,
    rho: 0.95,
    U: 250,
    T: 260,
    mu: 1.65e-5,
    kappa: 0.024,
    Cp: -0.2,
    Mach: 0.8,
    p_total: 130000,
    Tw: 280,
    yPlus: 45,
    tau_w: 12,
    Nu: 2400,
  }), []);

  const previewVal = useMemo(() => {
    if (!analysis || analysis.errors.length > 0) return null;
    return analysis.eval(sampleVals);
  }, [analysis, sampleVals]);

  const canAdd = analysis && analysis.errors.length === 0;

  const add = () => {
    if (!canAdd || !analysis) return;
    const f: DerivedField = { name: analysis.name, expr: analysis.expr, dim: dimToHuman(analysis.dim), dimVec: analysis.dim };
    setDerived((prev) => [...prev.filter((x) => x.name !== f.name), f]);
    onAdd?.(f);
  };

  return (
    <div className="flex flex-col gap-3 p-3 rounded-md border border-zinc-800 bg-[#111217]">
      <div className="flex items-center justify-between">
        <div>
          <h3 className="text-sm font-semibold text-zinc-100">Live SI-Checked Field Calculator</h3>
          <p className="text-[11px] text-zinc-500 font-mono">
            Rhai-style expressions with 7-vector dimensional propagation [kg,m,s,K,mol,A,cd]
          </p>
        </div>
        <button
          onClick={add}
          disabled={!canAdd}
          className="px-3 py-1.5 rounded bg-[#004AFD] text-white text-xs font-semibold disabled:bg-zinc-700 disabled:text-zinc-500 hover:bg-[#1a5fff] transition"
        >
          Upload to CFDFLD03
        </button>
      </div>

      <textarea
        className="w-full min-h-[96px] p-2.5 rounded bg-[#0b0c10] border border-zinc-800 font-mono text-[12.5px] text-emerald-200 focus:border-[#004AFD] outline-none resize-y"
        value={src}
        spellCheck={false}
        onChange={(e) => setSrc(e.target.value)}
      />

      {analysis && analysis.errors.length > 0 && (
        <div className="p-2.5 rounded border border-red-500/40 bg-red-500/10">
          {analysis.errors.map((e, i) => (
            <div key={i} className="text-[12px] text-red-300 font-mono leading-relaxed">
              <span className="text-red-400 font-bold">✖ </span>
              {e.message.split('\n').map((ln, j) => (
                <div key={j} className={j === 0 ? '' : 'pl-4 text-red-300/90'}>{ln}</div>
              ))}
              {e.suggestion && (
                <div className="mt-1 pl-4 text-amber-300 text-[11.5px]">{e.suggestion}</div>
              )}
            </div>
          ))}
        </div>
      )}

      {analysis && analysis.errors.length === 0 && (
        <div className="p-2.5 rounded border border-emerald-500/30 bg-emerald-500/5 font-mono text-[12px]">
          <div className="flex items-center gap-2">
            <span className="text-emerald-400 font-bold">✓</span>
            <span className="text-zinc-200">Derived field:</span>
            <span className="text-sky-300 font-semibold">{analysis.name}</span>
            <span className="text-zinc-500">→</span>
            <span className="text-violet-300">{dimToHuman(analysis.dim)}</span>
          </div>
          {previewVal !== null && Number.isFinite(previewVal) && (
            <div className="mt-1 pl-5 text-zinc-400">
              Sample value @ test point: <span className="text-zinc-200">{previewVal.toExponential(4)}</span>
            </div>
          )}
        </div>
      )}

      <div>
        <div className="text-[11px] font-semibold text-zinc-400 uppercase tracking-wider mb-1.5">Built-in fields</div>
        <div className="grid grid-cols-2 md:grid-cols-3 gap-1 max-h-40 overflow-auto pr-1">
          {FIELD_VARS.map((v) => (
            <div key={v.name} className="px-2 py-1 rounded bg-[#18181C] border border-zinc-800/60">
              <div className="flex items-center justify-between">
                <span className="text-sky-300 font-mono text-[11.5px] font-semibold">{v.name}</span>
                <span className="text-[10px] text-violet-300 font-mono">{dimToHuman(v.dim)}</span>
              </div>
              <div className="text-[10px] text-zinc-500 truncate">{v.label}</div>
            </div>
          ))}
        </div>
      </div>

      {derived.length > 0 && (
        <div>
          <div className="text-[11px] font-semibold text-zinc-400 uppercase tracking-wider mb-1.5">Derived (uploaded to GPU)</div>
          <div className="space-y-1">
            {derived.map((f) => (
              <div key={f.name} className="px-2 py-1 rounded bg-[#18181C] border border-emerald-700/30 flex items-center justify-between">
                <span className="font-mono text-emerald-300 text-[12px]">{f.name}</span>
                <span className="font-mono text-[10.5px] text-zinc-400 truncate max-w-[55%]">{f.expr}</span>
                <span className="text-[10px] text-violet-300 font-mono">{f.dim}</span>
              </div>
            ))}
          </div>
        </div>
      )}

      <details className="text-[11px] text-zinc-400">
        <summary className="cursor-pointer select-none hover:text-zinc-200">Example recipes</summary>
        <pre className="mt-1 p-2 rounded bg-[#0b0c10] border border-zinc-800 font-mono text-[11px] leading-5 text-zinc-300 overflow-x-auto">
{`# Total pressure (will fail if rho omitted!)
p_total = p + 0.5 * rho * dot(U, U)

# Mach number (vector magnitude / speed of sound)
mach = mag(U) / sqrt(gamma * R_air * T)

# Entropy generation rate (viscous + thermal)
s_gen = mu / T * mag_sq(grad(U))
       + kappa / (T*T) * mag_sq(grad(T))`}
        </pre>
      </details>
    </div>
  );
};
