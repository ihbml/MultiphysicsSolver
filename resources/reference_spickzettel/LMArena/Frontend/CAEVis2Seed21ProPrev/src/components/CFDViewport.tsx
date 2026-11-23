// WebGL2 (Three.js) viewport — simulates the wgpu R32Uint pick-buffer selection,
// Electric Blue (#004AFD) matched-face highlight, Red (#EF233C) overlap warning,
// and all the 3D gizmos (MRF cylinder, spray cone, mic sphere, farfield box, porous axes).

import React, { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import type { FacePatch, Gizmo, SpatialRule, Vec3 } from '../types';
import { buildMockPatches } from '../cfd-data';

export interface CameraState {
  position: [number, number, number];
  target: [number, number, number];
}

export interface ViewportProps {
  patches: FacePatch[];
  rules: SpatialRule[];
  gizmos: Gizmo[];
  selectedPatchId: number | null;
  onPickPatch: (patch: FacePatch) => void;
  lineProbe?: { pA: Vec3; pB: Vec3 } | null;
  highlightColor?: string;
  splitMode?: false | 'left' | 'right';
  label?: string;
  showField?: boolean;
  deltaMode?: boolean;
  // For A/B sync: when provided, viewport broadcasts camera state;
  // when `slaveTo` is set, viewport follows it.
  onCamera?: (s: CameraState) => void;
  slaveTo?: CameraState | null;
}

function makeFaceColor(rule: SpatialRule | undefined, selected: boolean, conflict: boolean): THREE.Color {
  if (conflict) return new THREE.Color('#EF233C'); // Red warning
  if (selected) return new THREE.Color('#004AFD'); // Electric Blue
  if (rule) return new THREE.Color(rule.color);
  return new THREE.Color('#3F4651');
}

// Build a box-like patch mesh from bbox + normal axis
function patchMesh(patch: FacePatch, color: THREE.Color): THREE.Mesh {
  const { bbox } = patch;
  const sx = bbox.max[0] - bbox.min[0];
  const sy = bbox.max[1] - bbox.min[1];
  const sz = bbox.max[2] - bbox.min[2];
  // Use thin box so that each patch appears as a face
  const geo = new THREE.BoxGeometry(Math.max(sx, 0.002), Math.max(sy, 0.002), Math.max(sz, 0.002));
  const mat = new THREE.MeshStandardMaterial({
    color,
    metalness: 0.2,
    roughness: 0.75,
    transparent: false,
    side: THREE.DoubleSide,
  });
  const mesh = new THREE.Mesh(geo, mat);
  const cx = (bbox.min[0] + bbox.max[0]) / 2;
  const cy = (bbox.min[1] + bbox.max[1]) / 2;
  const cz = (bbox.min[2] + bbox.max[2]) / 2;
  mesh.position.set(cx, cy, cz);
  mesh.userData.patchId = patch.id;
  mesh.userData.patch = patch;
  return mesh;
}

// NACA0012-ish airfoil extruded
function buildAirfoil(color: THREE.Color): THREE.Mesh {
  const pts: THREE.Vector2[] = [];
  const n = 40;
  // Upper surface
  for (let i = 0; i <= n; i++) {
    const x = i / n;
    const y = 0.06 * (0.2969 * Math.sqrt(x) - 0.126 * x - 0.3516 * x * x + 0.2843 * x * x * x - 0.1015 * x * x * x * x);
    pts.push(new THREE.Vector2(x - 0.5, y));
  }
  // Lower
  for (let i = n - 1; i >= 0; i--) {
    const x = i / n;
    const y = -0.06 * (0.2969 * Math.sqrt(x) - 0.126 * x - 0.3516 * x * x + 0.2843 * x * x * x - 0.1015 * x * x * x * x);
    pts.push(new THREE.Vector2(x - 0.5, y));
  }
  const shape = new THREE.Shape(pts);
  const geo = new THREE.ExtrudeGeometry(shape, { depth: 0.5, bevelEnabled: false });
  geo.rotateY(Math.PI / 2);
  geo.translate(1.5, 0, 0);
  const mat = new THREE.MeshStandardMaterial({ color, metalness: 0.3, roughness: 0.6, side: THREE.DoubleSide });
  const m = new THREE.Mesh(geo, mat);
  m.userData.patchId = 10;
  return m;
}

function makeMRFGizmo(g: Gizmo): THREE.Group {
  const group = new THREE.Group();
  const r = g.radius ?? 0.25;
  const h = g.height ?? 0.3;
  const cylGeo = new THREE.CylinderGeometry(r, r, h, 48, 1, true);
  const mat = new THREE.MeshBasicMaterial({ color: 0x004AFD, transparent: true, opacity: 0.15, side: THREE.DoubleSide });
  const cyl = new THREE.Mesh(cylGeo, mat);
  cyl.rotation.x = Math.PI / 2;
  group.add(cyl);
  // Rim rings
  const ringGeo = new THREE.TorusGeometry(r, 0.004, 8, 64);
  const ringMat = new THREE.MeshBasicMaterial({ color: 0x004AFD });
  const r1 = new THREE.Mesh(ringGeo, ringMat); r1.position.z = h / 2; r1.rotation.x = Math.PI / 2;
  const r2 = new THREE.Mesh(ringGeo, ringMat); r2.position.z = -h / 2; r2.rotation.x = Math.PI / 2;
  group.add(r1, r2);
  // Axis arrow
  const arrow = new THREE.ArrowHelper(new THREE.Vector3(0, 0, 1), new THREE.Vector3(0, 0, 0), h * 0.7, 0x004AFD, 0.04, 0.02);
  group.add(arrow);
  group.position.set(g.position[0], g.position[1], g.position[2]);
  group.userData.gizmo = g;
  return group;
}

function makeSprayGizmo(g: Gizmo): THREE.Group {
  const group = new THREE.Group();
  const axis = new THREE.Vector3(g.axis[0], g.axis[1], g.axis[2]).normalize();
  const theta = g.coneHalfAngle ?? (15 * Math.PI / 180);
  const length = 0.4;
  const coneGeo = new THREE.ConeGeometry(Math.tan(theta) * length, length, 32, 1, true);
  coneGeo.translate(0, length / 2, 0);
  coneGeo.rotateX(-Math.PI / 2);
  // Orient cone toward axis (default was +z after rotateX fix? default ConeGeometry points +y; we rotated so it points +z)
  const up = new THREE.Vector3(0, 0, 1);
  const q = new THREE.Quaternion().setFromUnitVectors(up, axis);
  coneGeo.applyQuaternion(q);
  const mat = new THREE.MeshBasicMaterial({ color: 0xF59E0B, transparent: true, opacity: 0.25, side: THREE.DoubleSide });
  const cone = new THREE.Mesh(coneGeo, mat);
  group.add(cone);
  const ringGeo = new THREE.TorusGeometry(Math.tan(theta) * length * 0.98, 0.003, 6, 48);
  ringGeo.applyQuaternion(q);
  ringGeo.translate(0, 0, length * 0.98);
  const ring = new THREE.Mesh(ringGeo, new THREE.MeshBasicMaterial({ color: 0xF59E0B }));
  group.add(ring);
  const sphere = new THREE.Mesh(new THREE.SphereGeometry(0.012, 16, 16), new THREE.MeshBasicMaterial({ color: 0xF59E0B }));
  group.add(sphere);
  group.position.set(g.position[0], g.position[1], g.position[2]);
  return group;
}

function makeMicGizmo(g: Gizmo): THREE.Group {
  const group = new THREE.Group();
  const dist = g.distanceSphere ?? 0.3;
  const sphereGeo = new THREE.SphereGeometry(dist, 32, 16);
  const mat = new THREE.MeshBasicMaterial({ color: 0x22C55E, transparent: true, opacity: 0.08, side: THREE.DoubleSide, wireframe: false });
  const sphere = new THREE.Mesh(sphereGeo, mat);
  group.add(sphere);
  const wire = new THREE.Mesh(sphereGeo, new THREE.MeshBasicMaterial({ color: 0x22C55E, wireframe: true, transparent: true, opacity: 0.4 }));
  group.add(wire);
  const center = new THREE.Mesh(new THREE.SphereGeometry(0.015, 12, 12), new THREE.MeshBasicMaterial({ color: 0x22C55E }));
  group.add(center);
  group.position.set(g.position[0], g.position[1], g.position[2]);
  return group;
}

function makeFarfieldGizmo(g: Gizmo): THREE.Group {
  const group = new THREE.Group();
  // 5L upstream, 15L downstream convention
  const L = 1.0; // characteristic length
  const up = 5 * L, down = 15 * L, height = 5 * L;
  const w = up + down;
  const gg = new THREE.BoxGeometry(w, height, height);
  const mat = new THREE.MeshBasicMaterial({ color: 0x60A5FA, transparent: true, opacity: 0.08, side: THREE.DoubleSide });
  const box = new THREE.Mesh(gg, mat);
  box.position.set(down - up, height / 2 + 0.5, 0);
  group.add(box);
  const edges = new THREE.EdgesGeometry(gg);
  const line = new THREE.LineSegments(edges, new THREE.LineBasicMaterial({ color: 0x60A5FA }));
  line.position.copy(box.position);
  group.add(line);
  group.position.set(g.position[0], g.position[1], g.position[2]);
  return group;
}

function makePorousGizmo(g: Gizmo): THREE.Group {
  const group = new THREE.Group();
  const axes = [
    { color: 0xEF4444, axis: new THREE.Vector3(1, 0, 0) },
    { color: 0x22C55E, axis: new THREE.Vector3(0, 1, 0) },
    { color: 0x3B82F6, axis: new THREE.Vector3(0, 0, 1) },
  ];
  for (const { color, axis } of axes) {
    const arrow = new THREE.ArrowHelper(axis, new THREE.Vector3(0, 0, 0), 0.18, color, 0.04, 0.02);
    group.add(arrow);
  }
  const sphere = new THREE.Mesh(new THREE.SphereGeometry(0.01, 12, 12), new THREE.MeshBasicMaterial({ color: 0xFFFFFF }));
  group.add(sphere);
  group.position.set(g.position[0], g.position[1], g.position[2]);
  return group;
}

function makeLineProbe(pA: Vec3, pB: Vec3): THREE.Group {
  const group = new THREE.Group();
  const pts = [new THREE.Vector3(...pA), new THREE.Vector3(...pB)];
  const g = new THREE.BufferGeometry().setFromPoints(pts);
  const line = new THREE.Line(g, new THREE.LineBasicMaterial({ color: 0x00FFAA }));
  group.add(line);
  const mkA = new THREE.Mesh(new THREE.SphereGeometry(0.015, 12, 12), new THREE.MeshBasicMaterial({ color: 0x00FFAA }));
  mkA.position.set(...pA);
  const mkB = new THREE.Mesh(new THREE.SphereGeometry(0.015, 12, 12), new THREE.MeshBasicMaterial({ color: 0x00FFAA }));
  mkB.position.set(...pB);
  group.add(mkA, mkB);
  return group;
}

export const CFDViewport: React.FC<ViewportProps> = ({
  patches, rules, gizmos, selectedPatchId, onPickPatch, lineProbe, label, splitMode, deltaMode,
  onCamera, slaveTo,
}) => {
  const hostRef = useRef<HTMLDivElement | null>(null);
  const stateRef = useRef<{
    renderer?: THREE.WebGLRenderer;
    scene?: THREE.Scene;
    camera?: THREE.PerspectiveCamera;
    controls?: OrbitControls;
    patchMeshes?: Map<number, THREE.Mesh>;
    ruleGroup?: THREE.Group;
    gizmoGroup?: THREE.Group;
    probeObj?: THREE.Group;
    raycaster?: THREE.Raycaster;
    pointer?: THREE.Vector2;
    raf?: number;
    lastCam?: CameraState;
    applyingSync?: boolean;
  }>({});
  const hoverRef = useRef<number | null>(null);
  const onCameraRef = useRef<ViewportProps['onCamera'] | undefined>(undefined);
  onCameraRef.current = onCamera;
  const slaveToRef = useRef<CameraState | null | undefined>(undefined);
  slaveToRef.current = slaveTo;

  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;

    const w = host.clientWidth;
    const h = host.clientHeight;
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(deltaMode ? '#0a0b0f' : (splitMode === 'right' ? '#0b0f14' : '#0d0f14'));
    const camera = new THREE.PerspectiveCamera(45, w / h, 0.01, 100);
    camera.position.set(2.5, 1.6, 3.6);
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, powerPreference: 'high-performance' });
    renderer.setPixelRatio(window.devicePixelRatio);
    renderer.setSize(w, h);
    host.appendChild(renderer.domElement);
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.target.set(1.0, 0.2, 0);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;

    // Lights
    const amb = new THREE.AmbientLight(0xffffff, 0.55);
    scene.add(amb);
    const dir = new THREE.DirectionalLight(0xffffff, 0.8);
    dir.position.set(5, 8, 5);
    scene.add(dir);
    const dir2 = new THREE.DirectionalLight(0x90b0ff, 0.25);
    dir2.position.set(-5, -2, -3);
    scene.add(dir2);

    // Ground grid
    const grid = new THREE.GridHelper(10, 40, 0x2A2D36, 0x1B1E26);
    grid.position.y = -0.5;
    scene.add(grid);

    // Patches
    const patchMeshes = new Map<number, THREE.Mesh>();
    for (const p of patches) {
      const m = patchMesh(p, new THREE.Color('#3F4651'));
      scene.add(m);
      patchMeshes.set(p.id, m);
    }
    // Airfoil shape
    const airfoil = buildAirfoil(new THREE.Color('#9AA0A6'));
    scene.add(airfoil);

    // Axes indicator
    const axesHelper = new THREE.AxesHelper(0.4);
    axesHelper.position.set(-1.2, -0.48, -0.5);
    scene.add(axesHelper);

    // Groups
    const gizmoGroup = new THREE.Group();
    scene.add(gizmoGroup);
    const probeObj = new THREE.Group();
    scene.add(probeObj);

    // Raycaster for picking
    const raycaster = new THREE.Raycaster();
    const pointer = new THREE.Vector2();

    stateRef.current = { renderer, scene, camera, controls, patchMeshes, gizmoGroup, probeObj, raycaster, pointer };

    // Picking interaction
    const onMove = (ev: MouseEvent) => {
      const rect = host.getBoundingClientRect();
      pointer.x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
      pointer.y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
      raycaster.setFromCamera(pointer, camera);
      const targets: THREE.Object3D[] = [];
      patchMeshes.forEach((m) => targets.push(m));
      targets.push(airfoil);
      const hits = raycaster.intersectObjects(targets, false);
      if (hits.length > 0) {
        const id = (hits[0].object as THREE.Mesh).userData.patchId as number | undefined;
        hoverRef.current = id ?? null;
        host.style.cursor = id ? 'pointer' : 'crosshair';
      } else {
        hoverRef.current = null;
        host.style.cursor = 'crosshair';
      }
    };
    const onClick = (ev: MouseEvent) => {
      const rect = host.getBoundingClientRect();
      pointer.x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
      pointer.y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
      raycaster.setFromCamera(pointer, camera);
      const targets: THREE.Object3D[] = [];
      patchMeshes.forEach((m) => targets.push(m));
      targets.push(airfoil);
      const hits = raycaster.intersectObjects(targets, false);
      if (hits.length > 0) {
        const id = (hits[0].object as THREE.Mesh).userData.patchId as number | undefined;
        if (id !== undefined) {
          const p = patches.find((pp) => pp.id === id);
          if (p) onPickPatch(p);
          else if (id === 10) {
            // treat airfoil click as suction patch (id 10)
            const ap = patches.find((pp) => pp.id === 10);
            if (ap) onPickPatch(ap);
          }
        }
      }
    };
    host.addEventListener('mousemove', onMove);
    host.addEventListener('click', onClick);

    // When user directly interacts with this viewport, break out of slave mode
    // (we are the master for the current drag)
    const onUserStart = () => { stateRef.current.applyingSync = false; };
    controls.addEventListener('start', onUserStart);
    controls.addEventListener('change', () => {
      stateRef.current.applyingSync = false;
    });

    let raf = 0;
    const animate = () => {
      raf = requestAnimationFrame(animate);
      const slave = slaveToRef.current;
      if (slave) {
        stateRef.current.applyingSync = true;
        camera.position.set(slave.position[0], slave.position[1], slave.position[2]);
        controls.target.set(slave.target[0], slave.target[1], slave.target[2]);
      }
      controls.update();
      // Emit camera state (master only)
      const cb = onCameraRef.current;
      if (cb && !stateRef.current.applyingSync) {
        const cs: CameraState = {
          position: [camera.position.x, camera.position.y, camera.position.z],
          target: [controls.target.x, controls.target.y, controls.target.z],
        };
        const prev = stateRef.current.lastCam;
        if (!prev ||
            Math.abs(prev.position[0] - cs.position[0]) > 1e-5 ||
            Math.abs(prev.position[1] - cs.position[1]) > 1e-5 ||
            Math.abs(prev.position[2] - cs.position[2]) > 1e-5 ||
            Math.abs(prev.target[0] - cs.target[0]) > 1e-5 ||
            Math.abs(prev.target[1] - cs.target[1]) > 1e-5 ||
            Math.abs(prev.target[2] - cs.target[2]) > 1e-5) {
          stateRef.current.lastCam = cs;
          cb(cs);
        }
      }
      renderer.render(scene, camera);
    };
    animate();

    const onResize = () => {
      const w2 = host.clientWidth, h2 = host.clientHeight;
      renderer.setSize(w2, h2);
      camera.aspect = w2 / h2;
      camera.updateProjectionMatrix();
    };
    const ro = new ResizeObserver(onResize);
    ro.observe(host);

    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
      host.removeEventListener('mousemove', onMove);
      host.removeEventListener('click', onClick);
      controls.removeEventListener('start', onUserStart);
      controls.dispose();
      renderer.dispose();
      if (renderer.domElement.parentNode) renderer.domElement.parentNode.removeChild(renderer.domElement);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Update patch colours when rules/selection change
  useEffect(() => {
    const s = stateRef.current;
    if (!s.patchMeshes) return;
    const idToRule = new Map<number, SpatialRule>();
    const idConflict = new Set<number>();
    for (const r of rules) {
      for (const fid of r.matchedFaceIds) {
        if (idToRule.has(fid)) idConflict.add(fid);
        else idToRule.set(fid, r);
      }
    }
    s.patchMeshes.forEach((mesh, id) => {
      const rule = idToRule.get(id);
      const conflict = idConflict.has(id);
      const sel = id === selectedPatchId;
      const col = makeFaceColor(rule, sel, conflict);
      (mesh.material as THREE.MeshStandardMaterial).color.copy(col);
    });
  }, [rules, selectedPatchId]);

  // Rebuild gizmos
  useEffect(() => {
    const s = stateRef.current;
    if (!s.gizmoGroup) return;
    while (s.gizmoGroup.children.length > 0) {
      const c = s.gizmoGroup.children.pop();
      if (c) {(c as any).geometry?.dispose?.(); }
    }
    for (const g of gizmos) {
      let obj: THREE.Group | null = null;
      if (g.kind === 'mrf') obj = makeMRFGizmo(g);
      else if (g.kind === 'spray') obj = makeSprayGizmo(g);
      else if (g.kind === 'mic') obj = makeMicGizmo(g);
      else if (g.kind === 'farfield') obj = makeFarfieldGizmo(g);
      else if (g.kind === 'porous') obj = makePorousGizmo(g);
      if (obj) s.gizmoGroup.add(obj);
    }
  }, [gizmos]);

  // Line probe
  useEffect(() => {
    const s = stateRef.current;
    if (!s.probeObj) return;
    while (s.probeObj.children.length > 0) {
      const c = s.probeObj.children.pop();
      if (c) {(c as any).geometry?.dispose?.(); }
    }
    if (lineProbe) {
      s.probeObj.add(makeLineProbe(lineProbe.pA, lineProbe.pB));
    }
  }, [lineProbe]);

  return (
    <div className="relative w-full h-full rounded-md overflow-hidden border border-zinc-800" style={{ background: '#0d0f14' }}>
      <div ref={hostRef} className="absolute inset-0" />
      {label && (
        <div className="absolute top-2 left-2 px-2 py-1 rounded bg-black/60 border border-zinc-700/60 text-[11px] font-mono text-zinc-300">
          {label}
        </div>
      )}
      <div className="absolute bottom-2 left-2 px-2 py-1 rounded bg-black/60 border border-zinc-700/60 text-[10px] font-mono text-zinc-400">
        Click a CAD face to synthesize a spatial rule · RMB-drag orbit · wheel zoom
      </div>
    </div>
  );
};

// Helper: mock patches list (re-export for convenience)
export const defaultPatches = buildMockPatches();
