/* No framework: a thin browser client for the actual C++ CLI via /api/run. */
'use strict';
const $ = (id) => document.getElementById(id);
const pretty = (n) => Number(n).toLocaleString();
const fmt = (n) => Number(n.toPrecision(5)).toString();
const state = { dimension: 2, preview: null, job: null, uploadBounds: null, hitPoints: [] };
const meshColors = { common: '#edf2f6', interpolation: '#acd5e2', sleeping: '#d7e5eb' };
const particleColors = { common: '#167596', transition: '#42b29b', interpolation: '#e4ad49' };
const presets = {
  example_gmsh: { dimension: 2, bounds: [[0, 0], [1, 1]], spacing: 0.055, core: 'ball' },
  example_foam: { dimension: 3, bounds: [[0, 0, 0], [2, 1, 1]], spacing: 0.4, core: 'all' },
  example_poly: { dimension: 3, bounds: [[0, 0, 0], [2, 2, 1]], spacing: 0.35, core: 'all' },
};

function requiredNumber(id) {
  const input = $(id), raw = input.value.trim();
  if (raw === '' || !Number.isFinite(Number(raw))) {
    input.focus();
    throw new Error(`Enter a valid number for ${input.labels?.[0]?.textContent || id}.`);
  }
  return Number(raw);
}
function currentBounds() {
  const kind = $('source').value;
  if (kind === 'box2' || kind === 'box3') {
    const dimension = kind === 'box2' ? 2 : 3;
    const lengths = ['lx', 'ly', 'lz'].slice(0, dimension).map(id => Number($(id).value) || 1);
    return [new Array(dimension).fill(0), lengths];
  }
  return presets[kind]?.bounds || state.uploadBounds ||
    (state.dimension === 3 ? [[0, 0, 0], [1, 1, 1]] : [[0, 0], [1, 1]]);
}
function setDimension(dimension) {
  state.dimension = dimension;
  document.querySelectorAll('.dim3').forEach(el => { el.hidden = dimension !== 3; });
}
function centerCore(selectBox = false) {
  const [lo, hi] = currentBounds();
  const ids = ['x', 'y', 'z'];
  for (let axis = 0; axis < state.dimension; axis++) {
    const span = hi[axis] - lo[axis];
    $(ids[axis] + 'min').value = fmt(lo[axis] + span * .25);
    $(ids[axis] + 'max').value = fmt(lo[axis] + span * .75);
    $('c' + ids[axis]).value = fmt(lo[axis] + span * .5);
  }
  $('radius').value = fmt(Math.min(...hi.map((v, i) => v - lo[i])) * .23);
  if (selectBox) $('core-mode').value = 'box';
  showCore();
}
function showCore() {
  const mode = $('core-mode').value;
  $('core-box').hidden = mode !== 'box';
  $('core-ball').hidden = mode !== 'ball';
  $('center-core').hidden = mode === 'all';
  $('core-hint').textContent = mode === 'all'
    ? 'Particles cover the complete domain; useful for inspecting physical-wall closures.'
    : 'Common particles inside; two SPH/FVM overlap layers outside.';
  if (mode === 'ball' && $('adapt-panel').open) $('adapt-panel').open = false;
}
function sourceChanged() {
  const kind = $('source').value;
  const isBox = kind === 'box2' || kind === 'box3';
  $('box-fields').hidden = !isBox;
  $('gmsh-fields').hidden = kind !== 'gmsh_upload';
  $('foam-fields').hidden = kind !== 'foam_upload';
  state.uploadBounds = null;
  if (kind === 'box2') {
    setDimension(2);
    $('nx').value = '30'; $('ny').value = '30';
    $('lx').value = '1'; $('ly').value = '1';
    $('spacing').value = fmt(1 / 30);
    $('core-mode').value = 'box';
    $('source-hint').textContent = 'A unit square is generated with 30 × 30 finite-volume cells.';
  } else if (kind === 'box3') {
    setDimension(3);
    $('nx').value = '14'; $('ny').value = '14'; $('nz').value = '14';
    $('lx').value = '1'; $('ly').value = '1'; $('lz').value = '1';
    $('spacing').value = fmt(1 / 14);
    $('core-mode').value = 'ball';
    $('source-hint').textContent = '3D preview shows a selectable 2D slice through the mesh.';
  } else if (presets[kind]) {
    setDimension(presets[kind].dimension);
    $('spacing').value = presets[kind].spacing;
    $('core-mode').value = presets[kind].core;
    $('source-hint').textContent = kind === 'example_gmsh'
      ? 'Jittered triangles with four physical boundary patches.'
      : 'Included ASCII OpenFOAM polyMesh fixture; no CFD fields are loaded.';
  } else {
    setDimension(kind === 'foam_upload' ? 3 : 2);
    $('core-mode').value = 'all';
    $('source-hint').textContent = kind === 'gmsh_upload'
      ? 'Select a Gmsh 2.2 ASCII file. Its bounds/dimension will be detected.'
      : 'Choose exactly: points, faces, owner, neighbour, boundary (ASCII).';
  }
  centerCore();
}
async function detectGmshFile() {
  const file = $('gmsh-file').files[0];
  if (!file) return;
  try {
    if (file.size > 2 * 1024 * 1024) throw new Error('File exceeds the 2 MB upload limit.');
    const text = await file.text();
    const section = text.match(/\$Nodes\s*\n(\d+)\s*\n([\s\S]*?)\$EndNodes/);
    if (!section) throw new Error('No $Nodes section found. Use ASCII Gmsh MSH 2.2.');
    const count = Number(section[1]);
    const lines = section[2].trim().split(/\r?\n/);
    if (lines.length !== count || count < 1) throw new Error('Invalid Gmsh node count.');
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (const line of lines) {
      const fields = line.trim().split(/\s+/).map(Number);
      if (fields.length !== 4 || !fields.every(Number.isFinite)) throw new Error('Invalid Gmsh coordinates.');
      for (let j = 0; j < 3; j++) { lo[j] = Math.min(lo[j], fields[j + 1]); hi[j] = Math.max(hi[j], fields[j + 1]); }
    }
    const dimension = hi[2] - lo[2] > 1e-9 * Math.max(1, hi[0] - lo[0], hi[1] - lo[1]) ? 3 : 2;
    state.uploadBounds = [lo.slice(0, dimension), hi.slice(0, dimension)];
    setDimension(dimension); centerCore();
    $('source-hint').textContent = `${dimension}D · ${pretty(count)} nodes · x ${fmt(lo[0])}…${fmt(hi[0])}, y ${fmt(lo[1])}…${fmt(hi[1])}`;
    clearError();
  } catch (error) { showError(error.message); }
}
async function detectFoamFiles() {
  const files = [...$('foam-files').files];
  $('foam-selected').textContent = files.length ? files.map(f => f.name).join(', ') :
    'points, faces, owner, neighbour, boundary · ASCII only.';
  const points = files.find(f => f.name === 'points');
  if (!points) return;
  try {
    if (points.size > 2 * 1024 * 1024) throw new Error('Points file exceeds 2 MB.');
    const text = await points.text();
    const positions = [...text.matchAll(/\(\s*([+-]?[\d.eE]+)\s+([+-]?[\d.eE]+)\s+([+-]?[\d.eE]+)\s*\)/g)];
    if (!positions.length) throw new Error('No ASCII point coordinates found.');
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (const match of positions) for (let j = 0; j < 3; j++) {
      const v = Number(match[j + 1]);
      if (!Number.isFinite(v)) throw new Error('Invalid OpenFOAM point.');
      lo[j] = Math.min(lo[j], v); hi[j] = Math.max(hi[j], v);
    }
    state.uploadBounds = [lo, hi];
    setDimension(3); centerCore(); clearError();
  } catch (error) { showError(error.message); }
}

async function payloadFromForm() {
  const type = $('source').value;
  let source;
  if (type === 'box2' || type === 'box3') {
    source = { type, nx: requiredNumber('nx'), ny: requiredNumber('ny'),
      lx: requiredNumber('lx'), ly: requiredNumber('ly') };
    if (type === 'box3') { source.nz = requiredNumber('nz'); source.lz = requiredNumber('lz'); }
  } else if (type === 'gmsh_upload') {
    const file = $('gmsh-file').files[0];
    if (!file) throw new Error('Choose a Gmsh .msh file first.');
    if (file.size > 2 * 1024 * 1024) throw new Error('Gmsh upload must be under 2 MB.');
    source = { type, text: await file.text() };
  } else if (type === 'foam_upload') {
    const files = [...$('foam-files').files];
    const names = ['points', 'faces', 'owner', 'neighbour', 'boundary'];
    if (files.length !== 5 || names.some(name => !files.some(file => file.name === name)))
      throw new Error('Choose exactly five OpenFOAM files: points, faces, owner, neighbour, boundary.');
    source = { type, files: {} };
    for (const file of files) {
      if (file.size > 2 * 1024 * 1024) throw new Error(`${file.name} exceeds 2 MB.`);
      source.files[file.name] = await file.text();
    }
  } else source = { type };
  let core;
  const mode = $('core-mode').value;
  if (mode === 'all') core = { type: 'all' };
  else if (mode === 'box') {
    const axes = ['x', 'y', 'z'].slice(0, state.dimension);
    core = { type: 'box', min: axes.map(a => requiredNumber(a + 'min')),
      max: axes.map(a => requiredNumber(a + 'max')) };
  } else {
    const axes = ['x', 'y', 'z'].slice(0, state.dimension);
    core = { type: 'ball', center: axes.map(a => requiredNumber('c' + a)),
      radius: requiredNumber('radius') };
  }
  const payload = { source, core, spacing: requiredNumber('spacing'),
    support: requiredNumber('support'), band: requiredNumber('band'),
    include_links: $('include-links').checked };
  const adaptFile = $('adapt-file').files[0];
  if ($('adapt-panel').open && adaptFile) {
    if (mode === 'ball') throw new Error('Adaptive threshold step requires a box or entire-domain core.');
    payload.adapt = { csv: await adaptFile.text(), margin: requiredNumber('margin'),
      step: requiredNumber('adapt-step') };
  }
  return payload;
}
function showError(message) { $('error').textContent = message; $('error').hidden = false; }
function clearError() { $('error').textContent = ''; $('error').hidden = true; }
function addDiagnostic(label, value) {
  const div = document.createElement('div');
  const dt = document.createElement('dt'), dd = document.createElement('dd');
  dt.textContent = label; dd.textContent = value;
  div.append(dt, dd); $('diagnostics').append(div);
}
function presentResults(data) {
  state.job = data.job_id;
  state.preview = data.preview;
  state.dimension = data.preview.dimension;
  if (['gmsh_upload', 'foam_upload'].includes($('source').value)) {
    state.uploadBounds = [data.preview.bounds[0].slice(0, state.dimension),
      data.preview.bounds[1].slice(0, state.dimension)];
  }
  setDimension(state.dimension);
  const s = data.summary;
  for (const [key, value] of [['cells', s.cells], ['faces', s.faces],
    ['particles', s.particles], ['boundary', s.physical_boundary_particles],
    ['links', s.sph_directed_links]]) $('stat-' + key).textContent = pretty(value);
  $('results-subtitle').textContent = `${s.dimension}D · completed in ${data.seconds.toFixed(2)} s · actual C++ mesher`;
  $('download-all').href = `/api/jobs/${data.job_id}/download`;
  $('download-all').hidden = false;
  $('diagnostics').replaceChildren();
  addDiagnostic('Common / transition / interpolation particles',
    [s.common_particles, s.transition_particles, s.interpolation_particles].map(pretty).join(' / '));
  addDiagnostic('Common / interpolation / sleeping cells',
    [s.common_mesh, s.interpolation_mesh, s.sleeping_mesh].map(pretty).join(' / '));
  addDiagnostic('P→M / M→P fallbacks · singular gradients',
    `${pretty(s.particle_to_mesh_fallbacks)} / ${pretty(s.mesh_to_particle_fallbacks)} · ${pretty(s.singular_particle_moments)}`);
  addDiagnostic('Centroid sample fallbacks', pretty(s.centroid_sample_fallbacks));
  const list = $('file-list'); list.replaceChildren();
  for (const filename of data.files) {
    const a = document.createElement('a');
    a.href = `/api/jobs/${data.job_id}/file/${filename}`;
    a.textContent = filename;
    a.download = filename;
    list.append(a);
  }
  $('backend-log').textContent = data.log || 'Run completed with no CLI output.';
  $('empty-visual').hidden = true;
  $('slice-controls').hidden = data.preview.dimension !== 3;
  $('plane').value = 'xy'; $('slice').value = '50';
  paint();
}
async function run(event) {
  event?.preventDefault();
  if ($('run-button').disabled) return;
  $('run-button').disabled = true;
  $('run-label').textContent = 'Running…';
  $('form-status').textContent = 'Preparing input and running the C++ mesher…';
  if (!state.preview) $('empty-visual').textContent = 'Running the C++ backend…';
  clearError();
  try {
    const payload = await payloadFromForm();
    const response = await fetch('/api/run', { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
    let data;
    try { data = await response.json(); } catch { throw new Error(`HTTP ${response.status}: invalid server response`); }
    if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
    presentResults(data);
    $('form-status').textContent = `Done: ${pretty(data.summary.particles)} particles generated.`;
  } catch (error) {
    showError(error.message || 'Run failed.');
    $('form-status').textContent = 'Run failed. Adjust the inputs and try again.';
    if (!state.preview) $('empty-visual').textContent = 'Nothing to preview yet';
  } finally {
    $('run-button').disabled = false;
    $('run-label').textContent = 'Run mesher';
  }
}

function planeInfo() {
  if (state.preview.dimension === 2) return { horizontal: 0, vertical: 1, depth: 2, label: 'XY' };
  const plane = $('plane').value;
  if (plane === 'xz') return { horizontal: 0, vertical: 2, depth: 1, label: 'XZ' };
  if (plane === 'yz') return { horizontal: 1, vertical: 2, depth: 0, label: 'YZ' };
  return { horizontal: 0, vertical: 1, depth: 2, label: 'XY' };
}
function paint() {
  if (!state.preview) return;
  const p = state.preview, canvas = $('preview-canvas'),
    width = canvas.clientWidth, height = canvas.clientHeight;
  if (!width || !height) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr);
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.fillStyle = '#fbfcfd'; ctx.fillRect(0, 0, width, height);
  const { horizontal: h, vertical: v, depth: d, label } = planeInfo();
  const low = p.bounds[0], high = p.bounds[1];
  const spanX = high[h] - low[h] || 1, spanY = high[v] - low[v] || 1;
  const margin = Math.min(39, Math.max(22, width * .06));
  const scale = Math.min((width - 2 * margin) / spanX, (height - 2 * margin) / spanY);
  const startX = (width - scale * spanX) / 2;
  const startY = (height + scale * spanY) / 2;
  const screen = (obj) => [startX + (obj[['x', 'y', 'z'][h]] - low[h]) * scale,
    startY - (obj[['x', 'y', 'z'][v]] - low[v]) * scale];
  const sliceValue = low[d] + (high[d] - low[d]) * Number($('slice').value) / 100;
  const tolerance = Math.max(p.spacing * .65, (high[d] - low[d]) * .015);
  const visible = (obj) => p.dimension === 2 || Math.abs(obj[['x', 'y', 'z'][d]] - sliceValue) <= tolerance;
  const x0 = startX, y0 = startY - spanY * scale;
  ctx.strokeStyle = '#d1dce3'; ctx.lineWidth = 1;
  ctx.strokeRect(x0, y0, spanX * scale, spanY * scale);
  ctx.fillStyle = '#8da1ae'; ctx.font = '11px system-ui';
  ctx.fillText(['X', 'Y', 'Z'][h], Math.min(width - 15, x0 + spanX * scale + 8), startY + 3);
  ctx.fillText(['X', 'Y', 'Z'][v], Math.max(5, x0 - 15), Math.max(14, y0 - 9));
  let shownCells = 0, shownParticles = 0;
  if ($('show-mesh').checked) {
    if (p.dimension === 2) {
      for (const cell of p.cells) {
        const vertices = cell.vertices.map(id => p.nodes[id]);
        if (vertices.some(vertex => !vertex)) continue;
        ctx.beginPath();
        vertices.forEach((vertex, index) => {
          const [x, y] = screen({ x: vertex[0], y: vertex[1], z: vertex[2] });
          if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        });
        ctx.closePath();
        ctx.fillStyle = meshColors[cell.role] || '#edf2f6'; ctx.fill();
        ctx.strokeStyle = '#a3b8c6'; ctx.lineWidth = .55; ctx.stroke();
        shownCells++;
      }
    } else {
      for (const cell of p.cells) {
        if (!visible(cell)) continue;
        const [x, y] = screen(cell);
        ctx.fillStyle = meshColors[cell.role] || '#edf2f6';
        ctx.strokeStyle = '#b8cbd4'; ctx.lineWidth = .6;
        ctx.fillRect(x - 3, y - 3, 6, 6); ctx.strokeRect(x - 3, y - 3, 6, 6);
        shownCells++;
      }
    }
  }
  state.hitPoints = [];
  if ($('show-particles').checked || $('show-wall').checked) {
    for (const particle of p.particles) {
      if (!visible(particle)) continue;
      if (!$('show-particles').checked && !particle.boundary) continue;
      const [x, y] = screen(particle);
      if (x < -5 || x > width + 5 || y < -5 || y > height + 5) continue;
      if ($('show-particles').checked) {
        ctx.beginPath(); ctx.arc(x, y, p.dimension === 2 ? 2.7 : 3.1, 0, Math.PI * 2);
        ctx.fillStyle = particleColors[particle.role] || '#167596'; ctx.fill();
      }
      if ($('show-wall').checked && particle.boundary) {
        ctx.beginPath(); ctx.arc(x, y, 5, 0, Math.PI * 2);
        ctx.strokeStyle = '#cf6044'; ctx.lineWidth = 1.8; ctx.stroke();
      }
      state.hitPoints.push({ x, y, particle });
      shownParticles++;
    }
  }
  if (p.dimension === 3) $('slice-label').textContent = `${['X', 'Y', 'Z'][d]} = ${fmt(sliceValue)}`;
  $('view-info').textContent = `${label} ${p.dimension === 3 ? 'slice' : 'map'} · ` +
    `${pretty(shownCells)} cells / ${pretty(shownParticles)} particles visible` +
    (p.limited ? ' · preview sampled; downloads are complete' : '');
  $('hover-tip').hidden = true;
}
function hover(event) {
  const tip = $('hover-tip'), rect = $('preview-canvas').getBoundingClientRect();
  const mx = event.clientX - rect.left, my = event.clientY - rect.top;
  let closest = null, d2 = 100;
  for (const point of state.hitPoints) {
    const distance = (point.x - mx) ** 2 + (point.y - my) ** 2;
    if (distance < d2) { d2 = distance; closest = point; }
  }
  if (!closest) { tip.hidden = true; return; }
  const particle = closest.particle;
  const patch = particle.patch >= 0 ?
    ` · patch: ${state.preview.patches[particle.patch] || particle.patch}` : '';
  tip.textContent = `Particle ${particle.id} · ${particle.role} · cell ${particle.owner}` +
    (particle.boundary ? ` · physical wall${patch}` : '');
  tip.style.left = `${Math.min(rect.width - 180, Math.max(5, mx + 12))}px`;
  tip.style.top = `${Math.max(5, my - 35)}px`;
  tip.hidden = false;
}

async function boot() {
  $('source').addEventListener('change', sourceChanged);
  $('core-mode').addEventListener('change', showCore);
  $('center-core').addEventListener('click', () => centerCore(true));
  $('gmsh-file').addEventListener('change', detectGmshFile);
  $('foam-files').addEventListener('change', detectFoamFiles);
  $('mesher-form').addEventListener('submit', run);
  for (const id of ['show-mesh', 'show-particles', 'show-wall', 'plane', 'slice'])
    $(id).addEventListener('input', paint);
  $('preview-canvas').addEventListener('mousemove', hover);
  $('preview-canvas').addEventListener('mouseleave', () => { $('hover-tip').hidden = true; });
  if ('ResizeObserver' in window) new ResizeObserver(paint).observe($('preview-canvas'));
  window.addEventListener('resize', paint);
  setDimension(2); showCore();
  try {
    const response = await fetch('/api/health');
    const data = await response.json();
    if (!data.ready) throw new Error('The C++ backend is not built. Run make -j4 and refresh.');
    $('engine-status').textContent = 'C++ backend ready';
    $('engine-status').classList.add('ready');
    await run(); // give the user a real, immediately inspectable default run
  } catch (error) {
    $('engine-status').textContent = 'Backend offline';
    $('engine-status').classList.add('offline');
    $('results-subtitle').textContent = 'Start the server after building the C++ backend.';
    $('form-status').textContent = error.message;
    $('empty-visual').textContent = 'No backend connection';
  }
}
boot();
