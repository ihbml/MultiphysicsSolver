import { useMemo, useState } from 'react';
import { Expand, MoveHorizontal, RotateCcw, SlidersHorizontal } from 'lucide-react';
import { motion } from 'framer-motion';
import { Modal } from './Modal';

interface Triangle { path: string; depth: number; angle: number; shade: number }
const wrapAngle = (value: number) => Math.atan2(Math.sin(value), Math.cos(value));

function Mesh({ yaw = -0.31, threshold = 25, large = false }: { yaw?: number; threshold?: number; large?: boolean }) {
  const triangles = useMemo(() => {
    const uCount = 46, vCount = 15;
    const points: { x: number; y: number; z: number }[][] = [];
    const project = (u: number, v: number) => {
      const radius = 79 + 29 * Math.cos(v);
      const x = radius * Math.cos(u), y = radius * Math.sin(u), z = 29 * Math.sin(v);
      const y1 = y * Math.cos(0.91) - z * Math.sin(0.91);
      const z1 = y * Math.sin(0.91) + z * Math.cos(0.91);
      const x2 = x * Math.cos(yaw) + z1 * Math.sin(yaw);
      const z2 = -x * Math.sin(yaw) + z1 * Math.cos(yaw);
      const angle = -0.16;
      return { x: 218 + (x2 * Math.cos(angle) - y1 * Math.sin(angle)) * 1.49, y: 126 + (x2 * Math.sin(angle) + y1 * Math.cos(angle)) * 1.49, z: z2 };
    };
    for (let i = 0; i <= uCount; i++) {
      points[i] = [];
      for (let j = 0; j <= vCount; j++) {
        const u = i / uCount * Math.PI * 2;
        const v = j / vCount * Math.PI * 2 + 0.045 * Math.sin(u * 5);
        points[i][j] = project(u, v);
      }
    }
    const result: Triangle[] = [];
    for (let i = 0; i < uCount; i++) for (let j = 0; j < vCount; j++) {
      const u = (i + 0.5) / uCount * Math.PI * 2;
      const v = (j + 0.5) / vCount * Math.PI * 2;
      const quality = 8 + 48 * Math.exp(-(wrapAngle(u - 0.6) ** 2) / 0.85 - (wrapAngle(v - 0.25) ** 2) / 2.8) + 4 * (1 + Math.sin(u * 3));
      const a = points[i][j], b = points[i+1][j], c = points[i+1][j+1], d = points[i][j+1];
      for (const vertices of [[a,b,c], [a,c,d]]) {
        const depth = vertices.reduce((sum, vertex) => sum + vertex.z, 0) / 3;
        result.push({ path: `M${vertices.map((vertex) => `${vertex.x.toFixed(2)},${vertex.y.toFixed(2)}`).join('L')}Z`, depth, angle: quality, shade: Math.round(19 + (depth + 100) / 28) });
      }
    }
    return result.sort((a, b) => a.depth - b.depth);
  }, [yaw]);
  const selected = triangles.filter((triangle) => triangle.angle > threshold).length;
  return (
    <svg className={`mesh-svg ${large ? 'mesh-large' : ''}`} viewBox="0 0 436 260" role="img" aria-label="Illustrative triangular torus mesh with orange correction region">
      <defs>
        <pattern id={`mesh-dots-${large ? 'large' : 'small'}`} width="17" height="17" patternUnits="userSpaceOnUse"><circle cx="1" cy="1" r="0.65" fill="currentColor" opacity="0.15" /></pattern>
        <radialGradient id={`mesh-fade-${large ? 'large' : 'small'}`}><stop offset="35%" stopColor="white" /><stop offset="100%" stopColor="black" /></radialGradient>
        <mask id={`mesh-mask-${large ? 'large' : 'small'}`}><rect width="436" height="260" fill={`url(#mesh-fade-${large ? 'large' : 'small'})`} /></mask>
      </defs>
      <rect width="436" height="260" fill={`url(#mesh-dots-${large ? 'large' : 'small'})`} mask={`url(#mesh-mask-${large ? 'large' : 'small'})`} />
      <g className="mesh-floor" stroke="currentColor" opacity="0.07" strokeWidth="0.7">
        {[0,1,2,3,4,5,6,7].map(i => <path key={`a${i}`} d={`M${30+i*48},244 L${105+i*34},162`} />)}
        {[0,1,2,3,4].map(i => <path key={`b${i}`} d={`M${45+i*9},${239-i*16} L${410-i*12},${239-i*16}`} />)}
      </g>
      <motion.g initial={{ opacity: 0, y: 6 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 1.2, ease: 'easeOut' }}>
        {triangles.map((triangle, index) => {
          const highlighted = triangle.angle > threshold;
          return <path key={index} d={triangle.path} fill={highlighted ? '#292017' : `rgb(${triangle.shade},${triangle.shade+1},${triangle.shade+3})`} stroke={highlighted ? '#da935b' : '#62666b'} strokeWidth={highlighted ? 0.67 : 0.47} strokeOpacity={highlighted ? 0.88 : 0.3 + (triangle.depth + 100) / 520} strokeLinejoin="round" />;
        })}
      </motion.g>
      <g transform="translate(32 218)" fill="none" strokeWidth="0.85" strokeLinecap="round">
        <path d="M0 0L22 4" stroke="#bd8563" /><path d="M0 0L-9 11" stroke="#737e77" /><path d="M0 0L0 -22" stroke="#798796" />
        <g fontSize="8" stroke="none" fontFamily="monospace"><text x="26" y="7" fill="#bd8563">x</text><text x="-17" y="18" fill="#737e77">y</text><text x="-3" y="-28" fill="#798796">z</text></g>
      </g>
      {large && <text x="422" y="245" textAnchor="end" fontSize="8" fill="#8c8d93" fontFamily="monospace">{selected} / {triangles.length} facets selected</text>}
    </svg>
  );
}

export function MeshPreview() {
  const [expanded, setExpanded] = useState(false);
  const [yaw, setYaw] = useState(-0.31);
  const [threshold, setThreshold] = useState(25);
  return (
    <>
      <div className="mesh-preview">
        <div className="mesh-title"><span className="mesh-dot" />MeshErrorMap<span className="mesh-sample">SAMPLE 001</span></div>
        <button className="mesh-open" onClick={() => setExpanded(true)} aria-label="Open interactive mesh explorer" title="Mesh explorer"><Expand size={15} /></button>
        <div className="mesh-drift"><Mesh /></div>
        <div className="mesh-legend"><span><i />Fast path</span><span><i className="orange" />Correction</span><span className="mesh-legend-note">Illustrative mesh</span></div>
      </div>
      {expanded && <Modal onClose={() => setExpanded(false)} className="mesh-dialog" label="MeshErrorMap explorer">
        <div className="eyebrow">INTERACTIVE GEOMETRY</div>
        <h2>MeshErrorMap explorer</h2>
        <p className="modal-description">Ein synthetisches Oberfl&auml;chennetz, um das Prinzip der Klassifizierung zu erkunden.</p>
        <div className="expanded-mesh"><Mesh yaw={yaw} threshold={threshold} large /></div>
        <div className="mesh-controls">
          <label><span><MoveHorizontal size={15} />Rotation <output>{Math.round(yaw * 180 / Math.PI)}&deg;</output></span><input type="range" min="-2.8" max="2.8" step="0.03" value={yaw} onChange={event => setYaw(Number(event.target.value))} /></label>
          <label><span><SlidersHorizontal size={15} />Correction threshold <output>{threshold}&deg;</output></span><input type="range" min="10" max="55" step="1" value={threshold} onChange={event => setThreshold(Number(event.target.value))} /></label>
          <button className="secondary-button" onClick={() => { setYaw(-0.31); setThreshold(25); }}><RotateCcw size={15} />Reset</button>
        </div>
        <p className="mesh-disclaimer">Das Farbfeld ist illustrativ, keine CFD-Fehlersch&auml;tzung. Ein echtes Dispatching verwendet dimensionslose Geometriema&szlig;e und unabh&auml;ngige Invalid-Pr&uuml;fungen.</p>
      </Modal>}
    </>
  );
}