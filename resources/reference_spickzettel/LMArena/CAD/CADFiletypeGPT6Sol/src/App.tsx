import { useRef, useState } from 'react';
import JSZip from 'jszip';
import { Activity, ArrowDownRight, ArrowRight, Box, Check, ChevronDown, CircleHelp, ClipboardList, Code2, Download, FileArchive, FileClock, FileJson2, GitBranch, Layers3, Menu, Plus, RotateCcw, SlidersHorizontal, Upload, X } from 'lucide-react';
import './index.css';

type Section = 'Overview' | 'File structure' | 'Operation graph' | 'Simulation bridge' | 'Materials' | 'Provenance';
type Operation = { id: string; type: string; name: string; parent: string | null; revision: number; createdAt: string };
type LogEntry = { action: string; detail: string; time: string };
const initialOperations: Operation[] = [
  { id: 'op_01a8', type: 'Sketch', name: 'Base profile', parent: null, revision: 1, createdAt: '2026-02-18T09:20:00Z' },
  { id: 'op_02b4', type: 'Extrude', name: 'Main body', parent: 'op_01a8', revision: 1, createdAt: '2026-02-18T09:24:00Z' },
  { id: 'op_03c2', type: 'Fillet', name: 'Edge rounds', parent: 'op_02b4', revision: 1, createdAt: '2026-02-18T09:28:00Z' },
  { id: 'op_04d6', type: 'Shell', name: 'Inner cavity', parent: 'op_03c2', revision: 1, createdAt: '2026-02-18T09:31:00Z' },
];
const initialLog: LogEntry[] = [
  { action: 'Model created', detail: 'Initial feature graph committed', time: '09:20' },
  { action: 'Feature added', detail: 'Main body extruded from base profile', time: '09:24' },
  { action: 'Feature added', detail: 'Inner cavity added to model', time: '09:31' },
];
const sections: { label: Section; icon: typeof Box }[] = [
  { label: 'Overview', icon: Box }, { label: 'File structure', icon: Layers3 },
  { label: 'Operation graph', icon: GitBranch }, { label: 'Simulation bridge', icon: Activity },
  { label: 'Materials', icon: SlidersHorizontal }, { label: 'Provenance', icon: FileClock },
];
const typeOptions = ['Boolean cut', 'Boolean union', 'Boolean intersect', 'Fillet', 'Chamfer', 'Extrude', 'Shell', 'Transform'];

function shortHash(value: string) {
  // A quick UI fingerprint; exported payload integrity uses SHA-256 instead.
  let hash = 2166136261;
  for (let i = 0; i < value.length; i++) hash = Math.imul(hash ^ value.charCodeAt(i), 16777619);
  return (hash >>> 0).toString(16).padStart(8, '0');
}
function fingerprint(op: Operation, operations: Operation[]): string {
  const parent = operations.find((item) => item.id === op.parent);
  return shortHash(`${op.id}:${op.type}:${op.name}:${op.revision}:${parent ? fingerprint(parent, operations) : 'root'}`);
}
function CadDrawing() {
  return <svg className="cad-drawing" viewBox="0 0 650 520" fill="none" aria-label="Isometric illustration of a machined CAD component" role="img">
    <defs>
      <linearGradient id="top" x1="180" y1="80" x2="489" y2="300" gradientUnits="userSpaceOnUse"><stop stopColor="#dfe5df"/><stop offset=".55" stopColor="#aebbb8"/><stop offset="1" stopColor="#788b8c"/></linearGradient>
      <linearGradient id="front" x1="205" y1="240" x2="365" y2="430" gradientUnits="userSpaceOnUse"><stop stopColor="#a4b2b1"/><stop offset="1" stopColor="#536c6d"/></linearGradient>
      <linearGradient id="side" x1="436" y1="230" x2="510" y2="370" gradientUnits="userSpaceOnUse"><stop stopColor="#687d7d"/><stop offset="1" stopColor="#344f52"/></linearGradient>
      <linearGradient id="hole" x1="271" y1="167" x2="361" y2="270" gradientUnits="userSpaceOnUse"><stop stopColor="#263d42"/><stop offset="1" stopColor="#657c7c"/></linearGradient>
      <filter id="shadow" x="40" y="40" width="570" height="480" filterUnits="userSpaceOnUse"><feGaussianBlur stdDeviation="25"/></filter>
      <pattern id="dots" width="19" height="19" patternUnits="userSpaceOnUse"><circle cx="1" cy="1" r=".7" fill="#cbd2cc"/></pattern>
    </defs>
    <rect width="650" height="520" fill="url(#dots)" opacity=".65"/>
    <path d="M108 403L329 492L559 365M108 403V367M329 492V455M559 365V328" stroke="#cbd3ce" strokeWidth="1"/>
    <ellipse cx="338" cy="421" rx="200" ry="47" fill="#4d6567" opacity=".17" filter="url(#shadow)"/>
    <path d="M165 195L319 105L504 190L347 282L165 195Z" fill="url(#top)" stroke="#315255" strokeWidth="2" strokeLinejoin="round"/>
    <path d="M165 195L347 282V392L165 302V195Z" fill="url(#front)" stroke="#315255" strokeWidth="2" strokeLinejoin="round"/>
    <path d="M347 282L504 190V300L347 392V282Z" fill="url(#side)" stroke="#315255" strokeWidth="2" strokeLinejoin="round"/>
    <path d="M194 196L319 123L476 193L347 268L194 196Z" stroke="#f4f7ef" opacity=".7"/>
    <path d="M181 209L181 292L347 373M363 284V373L489 296V211M166 246L347 336L504 244M166 276L347 366L504 274" stroke="#e5eee7" opacity=".25"/>
    <path d="M245 222C245 200 273 180 305 178C337 175 361 192 361 215V248C361 272 333 291 301 292C268 293 245 274 245 250V222Z" fill="url(#hole)" stroke="#28484a" strokeWidth="2"/>
    <ellipse cx="303" cy="216" rx="59" ry="34" transform="rotate(-7 303 216)" fill="#d7e0d9" stroke="#345558" strokeWidth="2"/>
    <ellipse cx="303" cy="216" rx="39" ry="20" transform="rotate(-7 303 216)" fill="#385256" stroke="#2b474a" strokeWidth="2"/>
    <path d="M269 225C277 205 319 197 337 210" stroke="#c8d5ce" strokeWidth="1.5" opacity=".65"/>
    <path d="M137 193H81M137 193L169 193M516 190H572M516 190L502 190M347 406V451M347 406V389" stroke="#7c948e"/>
    <circle cx="137" cy="193" r="3" fill="#8baa65"/><circle cx="516" cy="190" r="3" fill="#8baa65"/><circle cx="347" cy="406" r="3" fill="#8baa65"/>
    <text x="75" y="181" fill="#678078" fontFamily="monospace" fontSize="10" letterSpacing="1">B-REP</text><text x="532" y="178" fill="#678078" fontFamily="monospace" fontSize="10" letterSpacing="1">FACE_08</text><text x="290" y="468" fill="#678078" fontFamily="monospace" fontSize="10" letterSpacing="1">REGION_A</text>
    <path d="M122 104H197M122 104V134M524 369H574M574 369V335" stroke="#c6d0c7"/><text x="122" y="96" fill="#91a499" fontFamily="monospace" fontSize="10" letterSpacing="1">AX / 001</text>
  </svg>;
}
function PageHeading({ index, title, description }: { index: string; title: string; description: string }) {
  return <div className="page-heading"><div className="eyebrow"><span className="eyebrow-line"/>{index} / TECHNICAL DESIGN</div><h1>{title}</h1><p>{description}</p></div>;
}

export default function App() {
  const [section, setSection] = useState<Section>('Overview');
  const [operations, setOperations] = useState<Operation[]>(initialOperations);
  const [log, setLog] = useState<LogEntry[]>(initialLog);
  const [selectedId, setSelectedId] = useState('op_03c2');
  const [insertAt, setInsertAt] = useState<number | null>(null);
  const [newType, setNewType] = useState(typeOptions[0]);
  const [newName, setNewName] = useState('Mounting clearance');
  const [metallic, setMetallic] = useState(72);
  const [roughness, setRoughness] = useState(34);
  const [color, setColor] = useState('#8ba5a2');
  const [mobileNav, setMobileNav] = useState(false);
  const [notice, setNotice] = useState('');
  const fileInput = useRef<HTMLInputElement>(null);
  const selected = operations.find((op) => op.id === selectedId) || operations[0];
  const notify = (message: string) => { setNotice(message); window.setTimeout(() => setNotice(''), 4200); };
  const navigate = (next: Section) => { setSection(next); setMobileNav(false); window.scrollTo({ top: 0, behavior: 'smooth' }); };

  function insertOperation() {
    if (insertAt === null || !newName.trim()) return;
    const id = `op_${crypto.randomUUID().slice(0, 8)}`;
    const previous = operations[insertAt], next = operations[insertAt + 1];
    const inserted: Operation = { id, type: newType, name: newName.trim(), parent: previous.id, revision: 1, createdAt: new Date().toISOString() };
    const updated = operations.map((op) => op.id === next?.id ? { ...op, parent: id, revision: op.revision + 1 } : op);
    updated.splice(insertAt + 1, 0, inserted);
    setOperations(updated); setSelectedId(id); setInsertAt(null);
    setLog((items) => [...items, { action: 'Feature inserted', detail: `${newType} inserted after ${previous.name}; downstream dependency rewired`, time: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) }]);
    notify('Operation inserted. Downstream result fingerprints updated.');
  }
  function removeSelected() {
    const index = operations.findIndex((op) => op.id === selectedId);
    if (index < 1) return;
    const removed = operations[index];
    const updated = operations.filter((op) => op.id !== removed.id).map((op) => op.parent === removed.id ? { ...op, parent: removed.parent, revision: op.revision + 1 } : op);
    setOperations(updated); setSelectedId(updated[index - 1].id);
    setLog((items) => [...items, { action: 'Feature removed', detail: `${removed.name} removed; successor rewired to ${operations[index - 1].name}`, time: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) }]);
    notify('Operation removed. Graph dependencies repaired.');
  }
  async function downloadArchive() {
    try {
      const zip = new JSZip(), encoder = new TextEncoder();
      const files: Record<string, string> = {
        'history/operations.json': JSON.stringify({ graph: 'ordered-dag', operations }, null, 2),
        'materials/library.json': JSON.stringify({ materials: [{ id: 'mat_steel', name: 'Satin alloy', pbr: { baseColor: color, metallic: metallic / 100, roughness: roughness / 100 }, simulation: { density_kg_m3: 7850, role: 'solid' } }] }, null, 2),
        'geometry/preview.json': JSON.stringify({ note: 'Illustrative watertight box preview only. Exact B-rep requires a CAD kernel adapter.', units: 'm', vertices: [[0,0,0],[1,0,0],[1,1,0],[0,1,0],[0,0,1],[1,0,1],[1,1,1],[0,1,1]], triangles: [[0,2,1],[0,3,2],[4,5,6],[4,6,7],[0,1,5],[0,5,4],[1,2,6],[1,6,5],[2,3,7],[2,7,6],[3,0,4],[3,4,7]], regionId: 'region_solid' }, null, 2),
        'simulation/regions.json': JSON.stringify({ regions: [{ id: 'region_solid', role: 'solid', materialId: 'mat_steel' }], boundaries: [{ id: 'wall_outer', regionId: 'region_solid', kind: 'wall' }], meshHints: { targetEdgeLength_m: 0.05, preserveNamedFaces: true } }, null, 2),
        'provenance/events.json': JSON.stringify({ events: log, note: 'A production writer should append signed, immutable events.' }, null, 2),
        'README.txt': 'AXIOM .axcad format proposal v0.1\nThis is a working container prototype, not a production CAD kernel file.\nIt contains a watertight preview mesh, operation graph, materials, simulation metadata and provenance.\nExact B-rep payload and signed audit chain require a production implementation.\n',
      };
      const digests: Record<string, string> = {};
      for (const [path, content] of Object.entries(files)) {
        zip.file(path, content);
        const digest = await crypto.subtle.digest('SHA-256', encoder.encode(content));
        digests[path] = Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, '0')).join('');
      }
      zip.file('manifest.json', JSON.stringify({ format: 'AXIOM', extension: '.axcad', schemaVersion: '0.1.0', modelId: 'mdl_demo_001', units: { length: 'm', angle: 'rad' }, coordinateSystem: 'right-handed-z-up', geometry: { exactBrep: false, preview: 'geometry/preview.json' }, entrypoints: { operations: 'history/operations.json', materials: 'materials/library.json', simulation: 'simulation/regions.json', provenance: 'provenance/events.json' }, sha256: digests }, null, 2));
      const blob = await zip.generateAsync({ type: 'blob', compression: 'DEFLATE', compressionOptions: { level: 6 } });
      const url = URL.createObjectURL(blob), link = document.createElement('a');
      link.href = url; link.download = 'axiom-demo.axcad'; link.click();
      window.setTimeout(() => URL.revokeObjectURL(url), 1000);
      notify('Prototype .axcad archive downloaded.');
    } catch { notify('Archive could not be generated in this browser.'); }
  }
  async function importArchive(file?: File) {
    if (!file) return;
    try {
      const zip = await JSZip.loadAsync(file);
      const manifestText = await zip.file('manifest.json')?.async('string');
      const historyText = await zip.file('history/operations.json')?.async('string');
      if (!manifestText || !historyText) throw new Error('Missing required files');
      const manifest = JSON.parse(manifestText), history = JSON.parse(historyText);
      if (manifest.format !== 'AXIOM' || !Array.isArray(history.operations) || !history.operations.length || !manifest.sha256 || typeof manifest.sha256 !== 'object') throw new Error('Invalid AXIOM archive');
      if (['history/operations.json', 'materials/library.json', 'geometry/preview.json', 'simulation/regions.json', 'provenance/events.json'].some((path) => typeof manifest.sha256[path] !== 'string')) throw new Error('Missing required digests');
      const ids = new Set(history.operations.map((op: Operation) => op.id));
      if (history.operations.some((op: Operation, index: number) => !op.id || !op.type || !op.name || (index === 0 ? op.parent !== null : op.parent !== history.operations[index - 1].id || !ids.has(op.parent)))) throw new Error('Invalid operation graph');
      const encoder = new TextEncoder();
      for (const [path, expected] of Object.entries(manifest.sha256 || {})) {
        const text = await zip.file(path)?.async('string');
        if (!text) throw new Error('Missing archived content');
        const digest = await crypto.subtle.digest('SHA-256', encoder.encode(text));
        const actual = Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, '0')).join('');
        if (actual !== expected) throw new Error('Integrity check failed');
      }
      setOperations(history.operations); setSelectedId(history.operations[0].id);
      const eventText = await zip.file('provenance/events.json')?.async('string');
      if (eventText) { const parsed = JSON.parse(eventText); if (Array.isArray(parsed.events)) setLog(parsed.events); }
      const materialText = await zip.file('materials/library.json')?.async('string');
      if (materialText) { const material = JSON.parse(materialText).materials?.[0]?.pbr; if (material) { setColor(material.baseColor); setMetallic(Math.round(material.metallic * 100)); setRoughness(Math.round(material.roughness * 100)); } }
      navigate('Operation graph'); notify('Archive imported and SHA-256 integrity verified.');
    } catch { notify('Import failed: invalid or modified AXIOM archive.'); }
    if (fileInput.current) fileInput.current.value = '';
  }

  return <div className="app-shell">
    <aside className={`sidebar ${mobileNav ? 'sidebar-open' : ''}`}>
      <button className="brand" onClick={() => navigate('Overview')}><div className="brand-mark"><span/><span/><span/></div><div><strong>AXIOM<span className="brand-period">.</span></strong><small>ENGINEERING FORMAT</small></div></button>
      <div className="sidebar-rule"/><div className="nav-caption">EXPLORE THE FORMAT</div>
      <nav className="side-nav" aria-label="Main navigation">{sections.map(({ label, icon: Icon }) => <button key={label} className={`nav-item ${section === label ? 'active' : ''}`} onClick={() => navigate(label)}><Icon size={18} strokeWidth={1.7}/><span>{label}</span>{section === label && <span className="active-tick"/>}</button>)}</nav>
      <div className="sidebar-bottom"><div className="sidebar-note"><span className="status-dot"/><span>FORMAT PROPOSAL<br/><strong>Version 0.1 / Draft</strong></span></div><button className="sidebar-help" onClick={() => navigate('File structure')}><CircleHelp size={17}/> Read the architecture <ArrowRight size={15}/></button></div>
    </aside>
    <div className="workspace">
      <header className="topbar"><div className="topbar-left"><button className="mobile-menu" onClick={() => setMobileNav(!mobileNav)} aria-label="Toggle navigation"><Menu size={21}/></button><span className="breadcrumb-muted">Workspace</span><span className="breadcrumb-divider">/</span><span>{section}</span></div><div className="topbar-right"><span className="top-version">SPECIFICATION 0.1</span><span className="topbar-divider"/><button className="top-icon-button" onClick={() => fileInput.current?.click()} title="Import AXIOM archive" aria-label="Import AXIOM archive"><Upload size={17}/></button><button className="download-button" onClick={downloadArchive}><Download size={16}/><span>Download .axcad</span></button></div></header>
      <input ref={fileInput} type="file" accept=".axcad,.zip" className="hidden-input" onChange={(event) => importArchive(event.target.files?.[0])}/>
      <main className="main-content">
        {section === 'Overview' && <><section className="hero"><div className="hero-copy"><div className="eyebrow"><span className="eyebrow-line"/>AN OPEN FORMAT PROPOSAL FOR ENGINEERING</div><h1>AXIOM<span className="hero-dot">.</span></h1><h2>Geometry with<br/>a memory.</h2><p>A CAD container designed to carry exact intent, simulation-ready structure, material appearance, and a traceable history in one coherent file.</p><div className="hero-actions"><button className="primary-action" onClick={() => navigate('File structure')}>Explore the architecture <ArrowRight size={18}/></button><button className="text-action" onClick={() => navigate('Operation graph')}>Try the operation graph <ArrowDownRight size={17}/></button></div></div><div className="hero-art"><CadDrawing/><div className="drawing-caption"><span>FIG. 01</span><span>ONE MODEL / MULTIPLE DISCIPLINES</span></div></div></section><section className="overview-lower"><div className="section-kicker">THE DESIGN PRINCIPLE <span>01 / 04</span></div><div className="overview-statement"><h3>Not just a shape.<br/><em>A complete engineering record.</em></h3><p>AXIOM separates precise geometry from fast previews, while keeping operations, physical regions, and provenance connected by stable IDs.</p></div><div className="principle-list">{[['01', 'Exact where it matters', 'Kernel-native B-rep for editing; independently streamable LOD meshes for immediate display.'], ['02', 'Ready for solvers', 'Named volumes and boundary faces persist into FVM, CFD, and SPH workflows.'], ['03', 'Every change accounted for', 'An operation DAG and append-only events preserve why a model changed, not only what changed.']].map(([number, title, body]) => <div className="principle-row" key={number}><span>{number}</span><strong>{title}</strong><p>{body}</p><ArrowRight size={17}/></div>)}</div></section></>}

        {section === 'File structure' && <><PageHeading index="01" title="A container, not a monolith." description="A versioned ZIP container with a small manifest and independently addressable payloads. Read only the layers you need."/><div className="structure-layout"><div className="structure-tree"><div className="panel-label"><FileArchive size={17}/> AXIOM-PART.AXCAD <span>ZIP CONTAINER</span></div><div className="tree-lines"><div className="tree-row root"><FileJson2 size={17}/> manifest.json <span>required</span></div><div className="tree-row folder"><ChevronDown size={15}/> geometry/</div><div className="tree-row child"><span className="tree-branch">├─</span> brep.bin <span>exact</span></div><div className="tree-row child"><span className="tree-branch">└─</span> lod/preview.glb <span>fast</span></div><div className="tree-row folder"><ChevronDown size={15}/> history/</div><div className="tree-row child"><span className="tree-branch">└─</span> operations.json</div><div className="tree-row folder"><ChevronDown size={15}/> simulation/</div><div className="tree-row child"><span className="tree-branch">└─</span> regions.json</div><div className="tree-row folder"><ChevronDown size={15}/> materials/</div><div className="tree-row child"><span className="tree-branch">└─</span> library.json</div><div className="tree-row folder"><ChevronDown size={15}/> provenance/</div><div className="tree-row child"><span className="tree-branch">└─</span> events.json</div></div><div className="tree-foot">The downloadable prototype contains preview.json instead of a native B-rep. A CAD kernel adapter is required to write brep.bin.</div></div><div className="structure-detail"><div className="mini-label">MANIFEST / FIRST ENTRY TO READ</div><h2>Know what's inside<br/>before opening it.</h2><p>The manifest declares units, coordinate system, schema version, entrypoints, feature flags, and SHA-256 payload digests. A viewer can open the preview without loading the feature graph or exact surfaces.</p><div className="code-block"><div className="code-top"><span>manifest.json</span><span>JSON</span></div><pre>{`{\n  "format": "AXIOM",\n  "schemaVersion": "0.1.0",\n  "units": { "length": "m", "angle": "rad" },\n  "coordinateSystem": "right-handed-z-up",\n  "geometry": {\n    "exactBrep": true,\n    "preview": "geometry/lod/preview.glb"\n  },\n  "entrypoints": { "operations": "history/operations.json" },\n  "sha256": { "geometry/brep.bin": "..." }\n}`}</pre></div><a className="spec-link" href="/AXIOM_FORMAT_SPEC.md" download>Download technical specification <Download size={15}/></a></div></div><div className="spec-grid"><div><span>01 / PERFORMANCE</span><h3>Progressive by design</h3><p>Open the manifest and low-detail preview first. Lazy-load exact geometry, finer LODs, and history only when requested. Chunk large payloads and compress each entry independently.</p></div><div><span>02 / COMPATIBILITY</span><h3>Never guess units</h3><p>Declare a canonical coordinate frame and SI-scaled units. Version every schema, retain unknown optional fields, and use explicit migrations for breaking changes.</p></div><div><span>03 / IDENTITY</span><h3>Stable references</h3><p>Features, faces, regions, and materials get persistent IDs. Topological naming needs kernel-backed remapping when geometry changes; a face index is not an identity.</p></div></div></>}

        {section === 'Operation graph' && <><PageHeading index="02" title="Edit the past. Keep the future." description="Operations form an ordered dependency graph. Insert a feature between existing steps and downstream references are rewired, invalidated, and rebuilt without changing stable feature IDs."/><div className="graph-toolbar"><div><span className="live-indicator"/> LIVE GRAPH <span className="graph-toolbar-muted">/ {operations.length} FEATURES / REVISION {log.length}</span></div><button onClick={() => { setOperations(initialOperations); setLog(initialLog); setSelectedId('op_03c2'); notify('Demo graph reset.'); }}><RotateCcw size={15}/> Reset demo</button></div><div className="graph-layout"><div className="graph-main"><div className="graph-heading"><span>FEATURE HISTORY</span><span>SELECT AN OPERATION TO INSPECT</span></div><div className="feature-list">{operations.map((op, index) => <div key={op.id}><button className={`feature-row ${selectedId === op.id ? 'selected' : ''}`} onClick={() => setSelectedId(op.id)}><span className="feature-rail"><span className="feature-node"/></span><span className="feature-index">{String(index + 1).padStart(2, '0')}</span><span className="feature-icon">{op.type.includes('Boolean') ? <GitBranch size={18}/> : op.type === 'Sketch' ? <Code2 size={18}/> : <Box size={18}/>}</span><span className="feature-info"><strong>{op.name}</strong><small>{op.type} <span>·</span> {op.id}</small></span><span className="feature-hash">{fingerprint(op, operations)}</span><ArrowRight className="feature-arrow" size={17}/></button>{index < operations.length - 1 && <div className="insert-rail"><span className="rail-line"/><button onClick={() => { setInsertAt(index); setNewType('Boolean cut'); setNewName('Mounting clearance'); }} aria-label={`Insert operation after ${op.name}`}><Plus size={15}/> Insert operation here</button></div>}</div>)}</div><div className="graph-end"><span/> FINAL MODEL STATE <strong>{fingerprint(operations[operations.length - 1], operations)}</strong></div></div><aside className="inspector"><div className="inspector-top"><span>INSPECTOR</span><span className="inspector-live">● RESOLVED</span></div><div className="inspector-symbol"><GitBranch size={27} strokeWidth={1.3}/></div><div className="mini-label">{selected.type.toUpperCase()} / FEATURE</div><h2>{selected.name}</h2><p>Stable operation identity with an explicit parent dependency and reproducible result fingerprint.</p><div className="inspector-fields"><div><span>STABLE ID</span><strong>{selected.id}</strong></div><div><span>INPUT / PARENT</span><strong>{selected.parent || 'ROOT'}</strong></div><div><span>REVISION</span><strong>{String(selected.revision).padStart(2, '0')}</strong></div><div><span>RESULT FINGERPRINT</span><strong>{fingerprint(selected, operations)}</strong></div></div><button className="inspector-remove" disabled={!selected.parent} onClick={removeSelected}>Remove feature <ArrowRight size={15}/></button></aside></div><div className="graph-note"><div className="note-icon"><ClipboardList size={20}/></div><div><strong>What happens when you insert a Boolean?</strong><p>The new node points to its predecessor and its former successor points to the new node. This demo updates graph metadata and fingerprints only. A production CAD kernel must recompute downstream geometry and remap persistent face references, flagging ambiguous matches for review.</p></div></div></>}

        {section === 'Simulation bridge' && <><PageHeading index="03" title="From design to domain." description="A simulation handoff must preserve physical meaning, not just triangles. AXIOM carries named regions, face sets, units, and mesh intent alongside the geometry."/><div className="flow"><div><span>01 / SOURCE</span><Box size={30} strokeWidth={1.3}/><strong>Exact CAD</strong><p>Closed solids and named topology</p></div><ArrowRight className="flow-arrow" size={24}/><div><span>02 / PREPARE</span><Layers3 size={30} strokeWidth={1.3}/><strong>Domain model</strong><p>Fluid volumes and boundary sets</p></div><ArrowRight className="flow-arrow" size={24}/><div><span>03 / EXPORT</span><Activity size={30} strokeWidth={1.3}/><strong>Solver mesh</strong><p>Cells, particles, and fields</p></div></div><div className="section-divider-title"><span>INTEROPERABILITY CONTRACT</span><span>02 PATHS + 01 PREVIEW</span></div><div className="interop-list"><div><div className="interop-number">01</div><div><h3>CFD / FVM</h3><p>Export watertight fluid domains, volume and surface meshes, named inlet/outlet/wall face sets, region IDs and SI units. Use CGNS or VTK/VTU for meshes and fields; STEP AP242 for CAD exchange.</p></div><span className="interop-tag">CELL-BASED</span></div><div><div className="interop-number">02</div><div><h3>SPH</h3><p>Export a closed fluid volume and boundary geometry with particle spacing, seed strategy, normals, and stable boundary IDs. Particle generation belongs to the mesher, not the CAD container.</p></div><span className="interop-tag">PARTICLE-BASED</span></div><div><div className="interop-number">03</div><div><h3>Real-time rendering</h3><p>Stream glTF / GLB LODs with normals, UVs and PBR materials. The display mesh is a disposable cache; it is never treated as the editable source of truth.</p></div><span className="interop-tag">DISPLAY</span></div></div><div className="callout-line"><strong>Critical requirement</strong><p>Validate manifoldness, orientation, tolerances, and gaps before meshing. No file format can guarantee a valid CFD domain without geometry healing and a mesher-specific validation pass.</p></div></>}

        {section === 'Materials' && <><PageHeading index="04" title="Appearance meets physics." description="Rendering and simulation share a stable material ID, but keep their parameters separate. A metal's visual roughness is not its physical density."/><div className="material-layout"><div className="material-preview"><div className="preview-header"><span>ILLUSTRATIVE MATERIAL STUDY</span><span>MAT_STEEL_01</span></div><div className="material-sphere" style={{ background: `radial-gradient(circle at ${32 + (100 - roughness) * .15}% 27%, #ffffff 0%, ${color} 26%, #506765 64%, #182d2e 100%)`, filter: `saturate(${0.75 + metallic / 150})` }}/><div className="preview-floor"/><div className="preview-caption">SATIN ALLOY <span>PRINCIPLED MATERIAL</span></div></div><div className="material-controls"><div className="mini-label">MATERIAL / MAT_STEEL_01</div><h2>Satin alloy</h2><p>One identity, two independent parameter sets. Change the appearance below to see it reflected in the preview and downloaded archive.</p><label className="control-label">BASE COLOR <span>{color.toUpperCase()}</span></label><div className="color-control"><input type="color" value={color} onChange={(event) => setColor(event.target.value)} aria-label="Base color"/><span>{color}</span></div><label className="control-label" htmlFor="metallic">METALLIC <span>{metallic}%</span></label><input id="metallic" type="range" min="0" max="100" value={metallic} onChange={(event) => setMetallic(Number(event.target.value))}/><label className="control-label" htmlFor="roughness">ROUGHNESS <span>{roughness}%</span></label><input id="roughness" type="range" min="0" max="100" value={roughness} onChange={(event) => setRoughness(Number(event.target.value))}/><div className="physical-properties"><span>SIMULATION PROPERTIES <span className="lock-label">INDEPENDENT</span></span><div>Density <strong>7,850 kg/m³</strong></div><div>Domain role <strong>Solid</strong></div></div></div></div><div className="material-footer"><span>RENDER PIPELINE</span><p>Base color · metallic · roughness · normal · emissive · texture references. Use linear color internally, specify color spaces, and package texture assets by content hash.</p></div></>}

        {section === 'Provenance' && <><PageHeading index="05" title="A model you can account for." description="The file should explain who changed what, when, and from which inputs. Provenance is a first-class layer, not a filename convention."/><div className="provenance-layout"><div className="audit"><div className="section-divider-title"><span>EVENT HISTORY</span><span>{log.length} EVENTS</span></div>{[...log].reverse().map((event, index) => <div className="audit-row" key={`${event.time}-${index}`}><span className="audit-marker"/><div><strong>{event.action}</strong><p>{event.detail}</p></div><time>{event.time}</time></div>)}</div><div className="integrity"><div className="mini-label">TRUST MODEL</div><h2>Trace every<br/>transformation.</h2><p>In production, each operation should record its ID, parameters, inputs, author, timestamp and software version. This prototype stores graph metadata and SHA-256 payload hashes.</p><div className="integrity-rule"><Check size={17}/><span>Stable feature IDs across graph edits</span></div><div className="integrity-rule"><Check size={17}/><span>Content hashes for payload verification</span></div><div className="integrity-rule"><Check size={17}/><span>Append-only, signed events in production</span></div><div className="integrity-disclaimer">This browser prototype verifies payload hashes on import. Cryptographic signatures, author identity, immutable storage, and exact B-rep recomputation require a production backend and CAD kernel.</div></div></div></>}
      </main>
      <footer className="footer"><span>AXIOM / OPEN ENGINEERING FORMAT PROPOSAL</span><span>BUILT TO BE INSPECTED, NOT JUST OPENED.</span></footer>
    </div>
    {insertAt !== null && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget) setInsertAt(null); }}><div className="modal" role="dialog" aria-modal="true" aria-labelledby="modal-title"><div className="modal-top"><span>EDIT FEATURE HISTORY</span><button onClick={() => setInsertAt(null)} aria-label="Close dialog"><X size={20}/></button></div><div className="modal-icon"><Plus size={23}/></div><h2 id="modal-title">Insert an operation</h2><p>Place a new feature between <strong>{operations[insertAt].name}</strong> and <strong>{operations[insertAt + 1].name}</strong>. The downstream graph will be reconnected automatically.</p><label htmlFor="operation-type">OPERATION TYPE</label><select id="operation-type" value={newType} onChange={(event) => setNewType(event.target.value)}>{typeOptions.map((type) => <option key={type}>{type}</option>)}</select><label htmlFor="operation-name">FEATURE NAME</label><input id="operation-name" value={newName} onChange={(event) => setNewName(event.target.value)} onKeyDown={(event) => event.key === 'Enter' && insertOperation()} maxLength={60}/><div className="modal-actions"><button onClick={() => setInsertAt(null)}>Cancel</button><button className="primary-action" onClick={insertOperation} disabled={!newName.trim()}>Insert feature <ArrowRight size={16}/></button></div></div></div>}
    {notice && <div className="toast"><Check size={16}/>{notice}<button onClick={() => setNotice('')} aria-label="Dismiss"><X size={15}/></button></div>}
  </div>;
}
