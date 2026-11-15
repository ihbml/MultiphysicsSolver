import { useEffect, useRef, useState, type KeyboardEvent } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { hexToRgb } from '../lib/color';
import {
  bladeScalars,
  buildBlade,
  FIELD_BY_ID,
  sectionAt,
  SLICE_BOUNDS,
  SLICE_NAN_CELLS,
  SLICE_NAN_HALF,
  sliceValue,
  SPAN,
  type FieldId,
  type FlowState,
} from '../lib/flow';
import type { Shading } from '../lib/store';
import { HULL_FS, HULL_VS, PLAIN_FS, PLAIN_VS, SLICE_FS, SLICE_VS, SURF_FS, SURF_VS } from './shaders';

export type ViewPreset = 'iso' | 'suction' | 'pressure' | 'top';

export interface ProbeHit {
  x: number;
  y: number;
  value: number;
  nan: boolean;
  where: 'Wand' | 'Schnitt';
}

interface Props {
  field: FieldId;
  lut: Uint8Array;
  range: [number, number];
  bands: number;
  shading: Shading;
  edges: boolean;
  sliceOn: boolean;
  sliceS: number;
  nanOn: boolean;
  nanColor: string;
  hatch: boolean;
  flow: FlowState;
  view: { preset: ViewPreset; nonce: number };
  onProbe: (hit: ProbeHit | null) => void;
}

// ---------------------------------------------------------------- Kamera ----

const VIEWS: Record<ViewPreset, { dir: [number, number, number]; dist: number; target: [number, number, number] }> = {
  iso: { dir: [-0.6, 0.56, 0.58], dist: 13.4, target: [0.35, 2.4, 0.1] },
  suction: { dir: [0, 0.08, 1], dist: 11.5, target: [0.1, 2.45, 0.25] },
  pressure: { dir: [0, 0.08, -1], dist: 11.5, target: [0.1, 2.45, 0.25] },
  top: { dir: [0, 1, 0.0015], dist: 11, target: [0.5, 2.45, 0] },
};

interface Ctx {
  render: () => void;
  setView: (p: ViewPreset) => void;
  orbit: (dTheta: number, dPhi: number, zoom: number) => void;
  sync: (p: Props, recompute: boolean) => void;
  setLut: (lut: Uint8Array) => void;
}

export default function Viewport3D(props: Props) {
  const wrap = useRef<HTMLDivElement>(null);
  const canvas = useRef<HTMLCanvasElement>(null);
  const hud = useRef<HTMLSpanElement>(null);
  const triad = useRef<SVGSVGElement>(null);
  const ctx = useRef<Ctx | null>(null);
  const live = useRef(props);
  const [failed, setFailed] = useState(false);

  useEffect(() => {
    live.current = props;
  });

  // ------------------------------------------------------------ Aufbau ----
  useEffect(() => {
    const el = wrap.current;
    const cv = canvas.current;
    if (!el || !cv) return;

    let renderer: THREE.WebGLRenderer;
    try {
      renderer = new THREE.WebGLRenderer({ canvas: cv, antialias: true, alpha: true, powerPreference: 'high-performance' });
    } catch {
      setFailed(true);
      return;
    }
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    renderer.setClearColor(0x000000, 0);

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(32, 1, 0.1, 200);
    const controls = new OrbitControls(camera, cv);
    controls.enableDamping = false;
    controls.minDistance = 2.5;
    controls.maxDistance = 45;

    // 1D-LUT als Textur (256 × 1, sRGB-Bytes, unverändert durchgereicht)
    const lutTex = new THREE.DataTexture(new Uint8Array(256 * 4), 256, 1, THREE.RGBAFormat);
    lutTex.minFilter = THREE.LinearFilter;
    lutTex.magFilter = THREE.LinearFilter;
    lutTex.wrapS = THREE.ClampToEdgeWrapping;
    lutTex.generateMipmaps = false;
    lutTex.colorSpace = THREE.NoColorSpace;
    lutTex.needsUpdate = true;

    const shared = {
      uLut: { value: lutTex },
      uRange: { value: new THREE.Vector2(0, 1) },
      uBands: { value: 0 },
      uNanColor: { value: new THREE.Vector3(1, 0.4, 0.7) },
      uHatch: { value: 1 },
      uNanOn: { value: 0 },
      uPx: { value: renderer.getPixelRatio() },
    };

    // Schaufel
    const blade = buildBlade();
    const scalars = new Float32Array(blade.positions.length / 3);
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(blade.positions, 3));
    geo.setIndex(new THREE.BufferAttribute(blade.indices, 1));
    const scalarAttr = new THREE.BufferAttribute(scalars, 1);
    scalarAttr.setUsage(THREE.DynamicDrawUsage);
    geo.setAttribute('aScalar', scalarAttr);
    geo.setAttribute('aNan', new THREE.BufferAttribute(blade.nan, 1));
    geo.computeVertexNormals();

    const surfMat = new THREE.ShaderMaterial({
      uniforms: { ...shared, uMode: { value: 1 } },
      vertexShader: SURF_VS,
      fragmentShader: SURF_FS,
      side: THREE.DoubleSide,
      polygonOffset: true,
      polygonOffsetFactor: 1,
      polygonOffsetUnits: 1,
    });
    const bladeMesh = new THREE.Mesh(geo, surfMat);
    scene.add(bladeMesh);

    // Silhouette als invertierte Hülle: deutet die Form an, ohne Farbwerte zu verändern
    const hullMat = new THREE.ShaderMaterial({
      uniforms: { uWidth: { value: 0.022 } },
      vertexShader: HULL_VS,
      fragmentShader: HULL_FS,
      side: THREE.BackSide,
    });
    scene.add(new THREE.Mesh(geo, hullMat));

    // Oberflächennetz
    const edgeGeo = new THREE.BufferGeometry();
    edgeGeo.setAttribute('position', new THREE.BufferAttribute(blade.edges, 3));
    const edgeMat = new THREE.LineBasicMaterial({ color: 0x000000, transparent: true, opacity: 0.3 });
    const edgeLines = new THREE.LineSegments(edgeGeo, edgeMat);
    scene.add(edgeLines);

    // Plattform (Schaufelfuß) – trägt kein Feld, bleibt neutral grau
    const platGeo = new THREE.BoxGeometry(4.2, 0.5, 2.3);
    const platMat = new THREE.ShaderMaterial({ vertexShader: PLAIN_VS, fragmentShader: PLAIN_FS });
    const plat = new THREE.Mesh(platGeo, platMat);
    plat.position.set(0, -0.25, 0.3);
    plat.rotation.y = (4 * Math.PI) / 180; // folgt der Verwindung des Fußschnitts
    scene.add(plat);

    // Schnittebene: Feld wird je Pixel im Fragment-Shader ausgewertet
    const B = SLICE_BOUNDS;
    const sliceGeo = new THREE.BufferGeometry();
    sliceGeo.setAttribute(
      'position',
      new THREE.BufferAttribute(new Float32Array([B.x0, 0, B.z0, B.x1, 0, B.z0, B.x1, 0, B.z1, B.x0, 0, B.z1]), 3),
    );
    sliceGeo.setIndex([0, 1, 2, 0, 2, 3]);
    const sliceMat = new THREE.ShaderMaterial({
      uniforms: {
        ...shared,
        uField: { value: 0 },
        uSec: { value: new THREE.Vector4() },
        uXf: { value: new THREE.Vector4(1, 1, 0, 0) },
        uNanHalf: { value: SLICE_NAN_HALF },
        uNanCells: { value: SLICE_NAN_CELLS.map(([x, z]) => new THREE.Vector2(x, z)) },
      },
      vertexShader: SLICE_VS,
      fragmentShader: SLICE_FS,
      side: THREE.DoubleSide,
    });
    const slice = new THREE.Mesh(sliceGeo, sliceMat);
    scene.add(slice);
    const frameMat = new THREE.LineBasicMaterial({ color: 0x9aa3b2, transparent: true, opacity: 0.55 });
    const frameGeo = new THREE.BufferGeometry();
    frameGeo.setAttribute('position', sliceGeo.getAttribute('position'));
    const frame = new THREE.LineLoop(frameGeo, frameMat);
    scene.add(frame);

    // --------------------------------------------- Rendern nur bei Bedarf ----
    let raf = 0;
    let frames = 0;
    const tmpQ = new THREE.Quaternion();
    const tmpV = new THREE.Vector3();
    const AXES = [new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 0, 1)];
    const draw = () => {
      raf = 0;
      renderer.render(scene, camera);
      frames++;
      if (hud.current) hud.current.textContent = String(frames);
      const svg = triad.current;
      if (svg) {
        tmpQ.copy(camera.quaternion).invert();
        AXES.forEach((ax, i) => {
          tmpV.copy(ax).applyQuaternion(tmpQ);
          const line = svg.querySelector<SVGLineElement>(`[data-ax="${i}"]`);
          const text = svg.querySelector<SVGTextElement>(`[data-axl="${i}"]`);
          const x = 30 + tmpV.x * 19;
          const y = 30 - tmpV.y * 19;
          line?.setAttribute('x2', x.toFixed(1));
          line?.setAttribute('y2', y.toFixed(1));
          text?.setAttribute('x', (30 + tmpV.x * 26).toFixed(1));
          text?.setAttribute('y', (30 - tmpV.y * 26 + 3.5).toFixed(1));
        });
      }
    };
    const render = () => {
      if (!raf) raf = requestAnimationFrame(draw);
    };
    controls.addEventListener('change', render);

    const resize = () => {
      const w = el.clientWidth;
      const h = el.clientHeight;
      if (!w || !h) return;
      renderer.setSize(w, h, false);
      const aspect = w / h;
      camera.aspect = aspect;
      // schmale Viewports: horizontalen Bildwinkel halten, damit die Schaufel im Bild bleibt
      camera.fov = aspect >= 1.3 ? 32 : THREE.MathUtils.radToDeg(2 * Math.atan((Math.tan(THREE.MathUtils.degToRad(16)) * 1.3) / aspect));
      camera.updateProjectionMatrix();
      render();
    };
    const ro = new ResizeObserver(resize);
    ro.observe(el);

    const setView = (p: ViewPreset) => {
      const v = VIEWS[p];
      tmpV.set(v.dir[0], v.dir[1], v.dir[2]).normalize().multiplyScalar(v.dist);
      controls.target.set(v.target[0], v.target[1], v.target[2]);
      camera.position.copy(controls.target).add(tmpV);
      camera.up.set(0, 1, 0);
      controls.update();
      render();
    };

    const sph = new THREE.Spherical();
    const orbit = (dTheta: number, dPhi: number, zoom: number) => {
      tmpV.copy(camera.position).sub(controls.target);
      sph.setFromVector3(tmpV);
      sph.theta += dTheta;
      sph.phi = Math.min(Math.PI - 0.02, Math.max(0.02, sph.phi + dPhi));
      sph.radius = Math.min(controls.maxDistance, Math.max(controls.minDistance, sph.radius * zoom));
      tmpV.setFromSpherical(sph);
      camera.position.copy(controls.target).add(tmpV);
      controls.update();
      render();
    };

    const setLut = (lut: Uint8Array) => {
      (lutTex.image.data as Uint8Array).set(lut);
      lutTex.needsUpdate = true;
      render();
    };

    let wallActive = true;
    const sync = (p: Props, recompute: boolean) => {
      const def = FIELD_BY_ID[p.field];
      if (recompute) {
        bladeScalars(blade, p.field, p.flow, scalars);
        scalarAttr.needsUpdate = true;
      }
      const scalarMode = p.shading === 'unlit';
      wallActive = scalarMode && def.where !== 'slice';
      surfMat.uniforms.uMode.value = wallActive ? 1 : 0;
      const showSlice = scalarMode && def.where !== 'wall' && (p.sliceOn || def.where === 'slice');
      slice.visible = showSlice;
      frame.visible = showSlice;
      slice.position.y = p.sliceS * SPAN;
      frame.position.y = p.sliceS * SPAN;
      const sec = sectionAt(p.sliceS, p.flow.alphaInf);
      sliceMat.uniforms.uSec.value.set(sec.eps, sec.mu, sec.R, sec.beta);
      sliceMat.uniforms.uXf.value.set(sec.scale, sec.cosT, sec.sinT, sec.alpha);
      sliceMat.uniforms.uField.value = p.field === 'speed' ? 1 : p.field === 'schlieren' ? 2 : 0;
      edgeLines.visible = p.edges;
      shared.uRange.value.set(p.range[0], p.range[1]);
      shared.uBands.value = p.bands;
      shared.uHatch.value = p.hatch ? 1 : 0;
      shared.uNanOn.value = p.nanOn ? 1 : 0;
      const nc = hexToRgb(p.nanColor);
      shared.uNanColor.value.set(nc[0], nc[1], nc[2]);
      render();
    };

    // ------------------------------------------------------------ Probe ----
    const raycaster = new THREE.Raycaster();
    const ndc = new THREE.Vector2();
    const pa = new THREE.Vector3();
    const pb = new THREE.Vector3();
    const pc = new THREE.Vector3();
    const bary = new THREE.Vector3();
    const probeAt = (clientX: number, clientY: number) => {
      const p = live.current;
      if (p.shading !== 'unlit') return p.onProbe(null);
      const r = cv.getBoundingClientRect();
      ndc.set(((clientX - r.left) / r.width) * 2 - 1, -((clientY - r.top) / r.height) * 2 + 1);
      raycaster.setFromCamera(ndc, camera);
      const targets: THREE.Object3D[] = [bladeMesh];
      if (slice.visible) targets.push(slice);
      for (const h of raycaster.intersectObjects(targets, false)) {
        if (h.object === slice) {
          const sec = sectionAt(p.sliceS, p.flow.alphaInf);
          const v = sliceValue(p.field, sec, h.point.x, h.point.z);
          if (v === null) continue;
          const nan =
            p.nanOn &&
            SLICE_NAN_CELLS.some(([cx, cz]) => Math.abs(h.point.x - cx) < SLICE_NAN_HALF && Math.abs(h.point.z - cz) < SLICE_NAN_HALF);
          return p.onProbe({ x: clientX - r.left, y: clientY - r.top, value: v, nan, where: 'Schnitt' });
        }
        if (!h.face || !wallActive) return p.onProbe(null);
        const { a, b, c } = h.face;
        pa.fromArray(blade.positions, a * 3);
        pb.fromArray(blade.positions, b * 3);
        pc.fromArray(blade.positions, c * 3);
        THREE.Triangle.getBarycoord(h.point, pa, pb, pc, bary);
        const v = scalars[a] * bary.x + scalars[b] * bary.y + scalars[c] * bary.z;
        const nan = p.nanOn && blade.nan[a] * bary.x + blade.nan[b] * bary.y + blade.nan[c] * bary.z > 0.5;
        return p.onProbe({ x: clientX - r.left, y: clientY - r.top, value: v, nan, where: 'Wand' });
      }
      p.onProbe(null);
    };

    let probeRaf = 0;
    let down: { x: number; y: number } | null = null;
    const onMove = (e: PointerEvent) => {
      if (e.pointerType !== 'mouse' || e.buttons !== 0) return;
      const { clientX, clientY } = e;
      cancelAnimationFrame(probeRaf);
      probeRaf = requestAnimationFrame(() => probeAt(clientX, clientY));
    };
    const onDown = (e: PointerEvent) => {
      down = { x: e.clientX, y: e.clientY };
      cancelAnimationFrame(probeRaf);
      live.current.onProbe(null);
    };
    const onUp = (e: PointerEvent) => {
      if (down && e.pointerType !== 'mouse' && Math.hypot(e.clientX - down.x, e.clientY - down.y) < 8) probeAt(e.clientX, e.clientY);
      down = null;
    };
    const onLeave = () => {
      cancelAnimationFrame(probeRaf);
      live.current.onProbe(null);
    };
    cv.addEventListener('pointermove', onMove);
    cv.addEventListener('pointerdown', onDown);
    cv.addEventListener('pointerup', onUp);
    cv.addEventListener('pointerleave', onLeave);

    ctx.current = { render, setView, orbit, sync, setLut };
    resize();
    setView(live.current.view.preset);
    setLut(live.current.lut);
    sync(live.current, true);

    return () => {
      ctx.current = null;
      cancelAnimationFrame(raf);
      cancelAnimationFrame(probeRaf);
      ro.disconnect();
      cv.removeEventListener('pointermove', onMove);
      cv.removeEventListener('pointerdown', onDown);
      cv.removeEventListener('pointerup', onUp);
      cv.removeEventListener('pointerleave', onLeave);
      controls.dispose();
      [geo, edgeGeo, platGeo, sliceGeo, frameGeo].forEach((g) => g.dispose());
      [surfMat, hullMat, edgeMat, platMat, sliceMat, frameMat].forEach((m) => m.dispose());
      lutTex.dispose();
      renderer.dispose();
    };
  }, []);

  // --------------------------------------------------- Props → Uniforms ----
  const { field, flow, lut, view } = props;
  useEffect(() => {
    ctx.current?.setLut(lut);
  }, [lut]);

  useEffect(() => {
    ctx.current?.sync(live.current, true);
  }, [field, flow]);

  useEffect(() => {
    ctx.current?.sync(live.current, false);
  }, [props.range, props.bands, props.shading, props.edges, props.sliceOn, props.sliceS, props.nanOn, props.nanColor, props.hatch]);

  useEffect(() => {
    ctx.current?.setView(view.preset);
  }, [view]);

  const onKey = (e: KeyboardEvent) => {
    const c = ctx.current;
    if (!c) return;
    const step = Math.PI / 18;
    const map: Record<string, [number, number, number]> = {
      ArrowLeft: [-step, 0, 1],
      ArrowRight: [step, 0, 1],
      ArrowUp: [0, -step, 1],
      ArrowDown: [0, step, 1],
      '+': [0, 0, 0.88],
      '=': [0, 0, 0.88],
      '-': [0, 0, 1.14],
    };
    const m = map[e.key];
    if (!m) return;
    e.preventDefault();
    c.orbit(m[0], m[1], m[2]);
  };

  if (failed) {
    return (
      <div className="absolute inset-0 grid place-items-center p-6 text-center">
        <p className="max-w-[40ch] text-[14px] text-ink2">
          WebGL ist in diesem Browser nicht verfügbar. Der Viewport braucht eine GPU-Kontextfreigabe – Paletten, Colormaps und alle
          Referenzseiten funktionieren weiterhin.
        </p>
      </div>
    );
  }

  return (
    <div
      ref={wrap}
      tabIndex={0}
      role="application"
      aria-label="3D-Viewport. Ziehen dreht, Rad zoomt, Rechtsklick verschiebt. Pfeiltasten drehen, Plus und Minus zoomen."
      onKeyDown={onKey}
      className="absolute inset-0 focus-visible:outline-offset-[-2px]"
    >
      <canvas ref={canvas} className="block h-full w-full cursor-crosshair touch-none" />
      <div className="pointer-events-none absolute bottom-2 left-2 flex items-end gap-2">
        <svg ref={triad} width="60" height="60" viewBox="0 0 60 60" aria-hidden>
          {[
            ['#FF6B7C', 'X'],
            ['#B9F500', 'Y'],
            ['#84A6FF', 'Z'],
          ].map(([c, l], i) => (
            <g key={l}>
              <line data-ax={i} x1="30" y1="30" x2="30" y2="30" stroke={c} strokeWidth="1.5" strokeLinecap="round" />
              <text data-axl={i} x="30" y="30" fill={c} fontSize="10" textAnchor="middle" fontFamily="var(--font-mono)">
                {l}
              </text>
            </g>
          ))}
        </svg>
        <p className="num mb-1 text-[11px] leading-tight text-ink3" title="Gerendert wird nur bei Änderungen – im Stillstand 0 % GPU-Last">
          on-demand · <span ref={hud}>0</span> Frames
        </p>
      </div>
    </div>
  );
}
