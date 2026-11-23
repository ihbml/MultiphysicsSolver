import React, { useEffect, useRef, useState } from "react";
import {
  Box, Eye, Focus, Gauge, Grid3x3, Layers, Maximize2,
  MousePointer2, Move3d, Orbit, Ruler, Scissors, Sparkles, Target, TriangleAlert, Wrench,
} from "lucide-react";
import { useCockpit, ActorKind } from "../store";
import {
  buildBladeMesh, FIELDS, FieldKey, MeshData, patchByGroup, patchById, simFieldAt,
} from "../lib/sim";
import { oklabLUT, ColormapName, gradientCss } from "../lib/colormaps";
import {
  V3, add, cameraMatrices, cross, dot, invert4, mulPoint, norm, orbitEye, projectToScreen, scale, sub,
} from "../lib/math3d";
import { Chip, IconBtn, cn } from "./ui";

/* ------------------------------------------------------------------ */
/* Shaders                                                            */
/* ------------------------------------------------------------------ */
const BLADE_VS = `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in float aGroup;
layout(location=3) in vec2 aBase;
layout(location=4) in float aValA;
layout(location=5) in float aValB;
layout(location=6) in float aFlag;
uniform mat4 uVP;
out vec3 vWorld; out vec3 vNormal; out float vGroup; out vec2 vBase; out float vA; out float vB; out float vFlag;
void main(){
  vWorld = aPos; vNormal = aNormal; vGroup = aGroup; vBase = aBase; vA = aValA; vB = aValB; vFlag = aFlag;
  gl_Position = uVP * vec4(aPos, 1.0);
}`;

const BLADE_FS = `#version 300 es
precision highp float;
in vec3 vWorld; in vec3 vNormal; in float vGroup; in vec2 vBase; in float vA; in float vB; in float vFlag;
uniform sampler2D uLUT;
uniform int uMode;          // 0 A, 1 B, 2 delta
uniform float uMinA; uniform float uMaxA; uniform float uMinB; uniform float uMaxB; uniform float uDeltaSpan;
uniform vec3 uEye;
uniform int uHighlight;     // face group to outline in electric blue, -1 none
uniform int uSliceOn; uniform vec3 uSliceNormal; uniform float uSlicePos;
out vec4 outColor;

vec3 heat(float t){ return texture(uLUT, vec2(clamp(t, 0.0, 1.0), 0.5)).rgb; }

void main(){
  if (uSliceOn == 1 && dot(vWorld, uSliceNormal) > uSlicePos) discard;
  vec3 N = normalize(vNormal);
  if (!gl_FrontFacing) N = -N;
  float a = clamp((vA - uMinA) / max(0.0001, uMaxA - uMinA), 0.0, 1.0);
  float b = clamp((vB - uMinB) / max(0.0001, uMaxB - uMinB), 0.0, 1.0);
  float d = clamp(0.5 + (vB - vA) / max(0.0001, 2.0 * uDeltaSpan), 0.0, 1.0);
  vec3 base = uMode == 2 ? heat(d) : (uMode == 1 ? heat(b) : heat(a));
  // darken extremes slightly so form reads through the field
  base = mix(base, base * 0.72, 0.16);

  vec3 L1 = normalize(vec3(0.42, 0.80, 0.55));
  vec3 L2 = normalize(vec3(-0.65, 0.28, -0.42));
  float ndl = dot(N, L1);
  float wrap = clamp(ndl * 0.5 + 0.5, 0.0, 1.0);
  float diff = 0.24 + 0.70 * pow(wrap, 1.18);
  float rim = pow(1.0 - max(dot(N, normalize(uEye - vWorld)), 0.0), 3.0);
  float spec = pow(max(dot(reflect(-L1, N), normalize(uEye - vWorld)), 0.0), 42.0);

  vec3 col = base * diff;
  col += base * vec3(0.20, 0.36, 0.62) * max(dot(N, L2), 0.0) * 0.42;
  col += vec3(0.30, 0.47, 0.95) * rim * 0.30;
  col += vec3(1.0) * spec * 0.16;
  col = mix(vec3(0.035, 0.043, 0.055), col, 0.94);

  if (abs(vGroup - float(uHighlight)) < 0.5) {
    float edge = pow(1.0 - abs(dot(N, normalize(uEye - vWorld))), 1.4);
    col = mix(col, vec3(0.0, 0.29, 0.99), 0.44);
    col += vec3(0.06, 0.18, 0.6) * edge;
  }
  if (vFlag > 0.5) {
    col = mix(col, vec3(0.937, 0.137, 0.235), 0.62);
    col += vec3(0.35, 0.02, 0.04) * rim;
  }
  outColor = vec4(col, 1.0);
}`;

const PICK_VS = `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=2) in float aGroup;
uniform mat4 uVP;
out float vGroup;
void main(){ vGroup = aGroup; gl_Position = uVP * vec4(aPos, 1.0); }`;

const PICK_FS = `#version 300 es
precision highp float;
in float vGroup;
out uint outGroup;
void main(){ outGroup = uint(vGroup); }`;

const LINE_VS = `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uVP;
uniform float uPointSize;
out vec4 vColor;
void main(){ gl_Position = uVP * vec4(aPos, 1.0); gl_PointSize = uPointSize; vColor = aColor; }`;

const LINE_FS = `#version 300 es
precision highp float;
in vec4 vColor;
out vec4 outColor;
void main(){ outColor = vColor; }`;

/* ------------------------------------------------------------------ */
/* GL helpers                                                         */
/* ------------------------------------------------------------------ */
type Prog = { p: WebGLProgram; u: (n: string) => WebGLUniformLocation | null };

function createProgram(gl: WebGL2RenderingContext, vsSrc: string, fsSrc: string): Prog {
  const compile = (type: number, src: string) => {
    const sh = gl.createShader(type)!;
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh) || "shader compile failed");
    return sh;
  };
  const p = gl.createProgram()!;
  gl.attachShader(p, compile(gl.VERTEX_SHADER, vsSrc));
  gl.attachShader(p, compile(gl.FRAGMENT_SHADER, fsSrc));
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p) || "link failed");
  const cache = new Map<string, WebGLUniformLocation | null>();
  return {
    p,
    u: (n: string) => {
      if (!cache.has(n)) cache.set(n, gl.getUniformLocation(p, n));
      return cache.get(n)!;
    },
  };
}

const COLORMAP_FOR: Record<string, ColormapName> = {
  Cp: "turbo", p: "turbo", U: "turbo", T: "inferno", Mach: "turbo", yPlus: "inferno",
  tau_w: "turbo", Nu: "coolwarm", wallT: "inferno", delta: "coolwarm",
};

const AXIS_COLORS = { x: [0.95, 0.36, 0.42, 1], y: [0.35, 0.86, 0.65, 1], z: [0.4, 0.62, 1, 1] } as const;

/* ------------------------------------------------------------------ */
/* Procedural gizmo + scene geometry                                  */
/* ------------------------------------------------------------------ */
type LineArrays = { pos: number[]; col: number[] };

function pushSeg(arr: LineArrays, a: V3, b: V3, c: readonly number[]) {
  arr.pos.push(...a, ...b);
  arr.col.push(...c, ...c);
}

function circle(arr: LineArrays, center: V3, radius: number, axis: 0 | 1 | 2, color: readonly number[], seg = 48) {
  for (let i = 0; i < seg; i++) {
    const t0 = (i / seg) * Math.PI * 2, t1 = ((i + 1) / seg) * Math.PI * 2;
    const mk = (t: number): V3 => {
      const u = Math.cos(t) * radius, v = Math.sin(t) * radius;
      return axis === 0 ? [center[0], center[1] + u, center[2] + v]
        : axis === 1 ? [center[0] + u, center[1], center[2] + v]
        : [center[0] + u, center[1] + v, center[2]];
    };
    pushSeg(arr, mk(t0), mk(t1), color);
  }
}

function arrow(arr: LineArrays, a: V3, b: V3, color: readonly number[], head = 0.1) {
  pushSeg(arr, a, b, color);
  const dir = norm(sub(b, a));
  const perp = norm(cross(dir, Math.abs(dir[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0]));
  const base = sub(b, scale(dir, head));
  pushSeg(arr, b, add(base, scale(perp, head * 0.45)), color);
  pushSeg(arr, b, sub(base, scale(perp, head * 0.45)), color);
}

function buildScene(state: ReturnType<typeof useCockpit>, domain: any): { lines: LineArrays; fill: LineArrays } {
  const lines: LineArrays = { pos: [], col: [] };
  const fill: LineArrays = { pos: [], col: [] };
  const dim = [0.42, 0.46, 0.55, 0.55];
  const cyanish = [0.25, 0.88, 1.0, 0.62];

  // floor grid
  for (let i = -8; i <= 8; i++) {
    const g = i % 4 === 0 ? [0.2, 0.22, 0.28, 0.7] : [0.14, 0.155, 0.19, 0.5];
    pushSeg(lines, [-8 + i, -0.78, -6], [-8 + i, -0.78, 6], g);
    pushSeg(lines, [-8, -0.78, -8 + i], [8, -0.78, -8 + i], g);
  }
  // hub plane
  circle(lines, [0, -0.75, 0], 1.05, 1, [0.24, 0.28, 0.36, 0.6], 64);

  const L = 1.72;
  const x0 = -1.05 - domain.upstream * 0.2;
  const x1 = 0.92 + domain.downstream * 0.065;
  const half = domain.radial * 0.36;
  const yb = -0.95, yt = 0.95 + (domain.radial - 5) * 0.03;
  const zf = -half, zb = half;

  if (domain.showDomain) {
    const corners: V3[] = [
      [x0, yb, zf], [x1, yb, zf], [x1, yb, zb], [x0, yb, zb],
      [x0, yt, zf], [x1, yt, zf], [x1, yt, zb], [x0, yt, zb],
    ];
    const edges: [number, number][] = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
    edges.forEach(([a, b]) => pushSeg(lines, corners[a], corners[b], dim));
    // upstream / downstream braces on the top plane
    const brace = (x: number, col: readonly number[]) => {
      pushSeg(lines, [x, yt, zf], [x, yt + 0.12, zf], col);
      pushSeg(lines, [x, yt, zb], [x, yt + 0.12, zb], col);
      pushSeg(lines, [x, yt + 0.12, zf], [x, yt + 0.12, zb], col);
    };
    brace(x0, AXIS_COLORS.z);
    brace(x1, AXIS_COLORS.x);
    // faint axial centerline
    pushSeg(lines, [x0 - 0.4, 0, 0], [x1 + 0.5, 0, 0], [0.28, 0.32, 0.42, 0.5]);
    // section markers
    for (let i = 1; i < 6; i++) {
      const x = x0 + ((x1 - x0) * i) / 6;
      pushSeg(lines, [x, yb, zf], [x, yb + 0.07, zf], [0.3, 0.33, 0.42, 0.5]);
    }
  }

  if (domain.showMrf) {
    const r = domain.mrfRadius;
    const hh = domain.mrfHeight / 2;
    const yTop = 0.18 + hh;
    const yBot = 0.18 - hh;
    for (let i = 0; i < 8; i++) {
      const a = (i / 8) * Math.PI * 2;
      const px = Math.cos(a) * r, pz = Math.sin(a) * r;
      pushSeg(lines, [px, yBot, pz], [px, yTop, pz], cyanish);
    }
    [yBot, 0.18, yTop].forEach((y, idx) => circle(lines, [0, y, 0], r, 1, idx === 1 ? [0.25, 0.88, 1, 0.75] : cyanish, 64));
    // translucent shell
    const seg = 48;
    for (let i = 0; i < seg; i++) {
      const a0 = (i / seg) * Math.PI * 2, a1 = ((i + 1) / seg) * Math.PI * 2;
      const p0: V3 = [Math.cos(a0) * r, yBot, Math.sin(a0) * r];
      const p1: V3 = [Math.cos(a1) * r, yBot, Math.sin(a1) * r];
      const p2: V3 = [Math.cos(a1) * r, yTop, Math.sin(a1) * r];
      const p3: V3 = [Math.cos(a0) * r, yTop, Math.sin(a0) * r];
      fill.pos.push(...p0, ...p1, ...p2, ...p0, ...p2, ...p3);
      for (let k = 0; k < 6; k++) fill.col.push(0.09, 0.35, 0.62, i % 2 ? 0.055 : 0.085);
    }
    // rotation arrow around axis
    const rotR = r + 0.1;
    for (let i = 0; i < 9; i++) {
      const a0 = (i / 12) * Math.PI * 2, a1 = a0 + 0.28;
      pushSeg(lines, [Math.cos(a0) * rotR, yTop + 0.04, Math.sin(a0) * rotR], [Math.cos(a1) * rotR, yTop + 0.04, Math.sin(a1) * rotR], [0.25, 0.88, 1, 0.8]);
    }
    arrow(lines, [0, yTop + 0.02, 0], [0, yTop + 0.34, 0], [0.25, 0.88, 1, 0.8], 0.07);
  }

  // blade chord ticks projection on floor
  for (let i = 0; i <= 10; i++) {
    const x = -0.85 + (L * i) / 10;
    pushSeg(lines, [x, -0.77, -0.9], [x, -0.77, 0.9], [0.13, 0.15, 0.2, 0.45]);
  }

  if (!state.actorsVisible) return { lines, fill };

  // DPM injectors
  state.injectors.forEach((inj) => {
    if (!inj.enabled) return;
    const axis = norm(inj.axis);
    const len = 0.62;
    const tip = add(inj.pos, scale(axis, len));
    const p1 = norm(cross(axis, Math.abs(axis[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0]));
    const p2 = cross(axis, p1);
    const rad = len * Math.tan((inj.cone * Math.PI) / 180) * 1.35;
    const seg = 32;
    const orange = [1.0, 0.69, 0.13, 0.92];
    const orangeSoft = [1.0, 0.62, 0.1, 0.4];
    for (let i = 0; i < seg; i++) {
      const a0 = (i / seg) * Math.PI * 2, a1 = ((i + 1) / seg) * Math.PI * 2;
      const rim = (a: number): V3 => add(tip, add(scale(p1, Math.cos(a) * rad), scale(p2, Math.sin(a) * rad)));
      pushSeg(lines, rim(a0), rim(a1), i % 2 ? orangeSoft : orange);
      if (i % 4 === 0) pushSeg(lines, inj.pos, rim(a0), [1, 0.55, 0.1, 0.28]);
    }
    for (let i = 0; i < 5; i++) {
      const a = (i / 5) * Math.PI * 2;
      const rim = add(tip, add(scale(p1, Math.cos(a) * rad), scale(p2, Math.sin(a) * rad)));
      pushSeg(lines, add(inj.pos, scale(axis, 0.05)), rim, [1, 0.62, 0.1, 0.2]);
    }
    arrow(lines, inj.pos, tip, orange, 0.09);
    circle(lines, add(inj.pos, scale(axis, 0.08)), 0.07, 1, orange, 20);
  });

  // FWH microphones
  state.mics.forEach((m) => {
    if (!m.enabled) return;
    const c = [0.32, 0.9, 0.72, 0.65] as const;
    circle(lines, m.pos, m.radius, 0, c, 40);
    circle(lines, m.pos, m.radius, 1, c, 40);
    circle(lines, m.pos, m.radius, 2, c, 40);
    pushSeg(lines, m.pos, add(m.pos, [0, m.radius, 0]), c);
    pushSeg(lines, m.pos, add(m.pos, [m.radius, 0, 0]), c);
    pushSeg(lines, m.pos, add(m.pos, [0, 0, m.radius]), c);
    // observer ray
    pushSeg(lines, m.pos, [0, 0.1, 0], [0.32, 0.9, 0.72, 0.18]);
  });

  // anisotropic porous block + principal axes
  state.porous.forEach((p) => {
    if (!p.enabled) return;
    const e1 = norm(p.e1), e2 = norm(p.e2), e3 = norm(p.e3);
    const h = 0.24;
    const corner = (sx: number, sy: number, sz: number): V3 =>
      add(p.pos, add(scale(e1, sx * h), add(scale(e2, sy * h * 0.72), scale(e3, sz * h * 0.9))));
    const C = (sx: number, sy: number, sz: number) => corner(sx, sy, sz);
    const edge = (a: V3, b: V3, col: readonly number[]) => pushSeg(lines, a, b, col);
    const boxCol = [0.65, 0.56, 0.98, 0.55] as const;
    const xs = [-1, 1], ys = [-1, 1], zs = [-1, 1];
    xs.forEach((sx) => ys.forEach((sy) => edge(C(sx, sy, -1), C(sx, sy, 1), boxCol)));
    ys.forEach((sy) => zs.forEach((sz) => edge(C(-1, sy, sz), C(1, sy, sz), boxCol)));
    xs.forEach((sx) => zs.forEach((sz) => edge(C(sx, -1, sz), C(sx, 1, sz), boxCol)));
    arrow(lines, p.pos, add(p.pos, scale(e1, 0.5)), AXIS_COLORS.x, 0.07);
    arrow(lines, p.pos, add(p.pos, scale(e2, 0.46)), AXIS_COLORS.y, 0.07);
    arrow(lines, p.pos, add(p.pos, scale(e3, 0.42)), AXIS_COLORS.z, 0.07);
  });

  // probe rake / chord handles
  if (state.workspace === "probe" || state.workspace === "rules") {
    const A = state.probe.a, B = state.probe.b;
    pushSeg(lines, A, B, [0.86, 0.9, 1, 0.95]);
    const dir = norm(sub(B, A));
    const perp = norm(cross(dir, Math.abs(dir[1]) > 0.92 ? [1, 0, 0] : [0, 1, 0]));
    const n = 22;
    for (let i = 0; i <= n; i++) {
      const p = add(A, scale(sub(B, A), i / n));
      const tick = i % 5 === 0 ? 0.075 : 0.038;
      pushSeg(lines, p, add(p, scale(perp, tick)), i % 5 === 0 ? [0.75, 0.82, 1, 0.85] : [0.5, 0.58, 0.8, 0.45]);
    }
    // surface chord guide on the blade
    if (state.probe.kind === "chord") {
      for (let i = 0; i <= 40; i++) {
        const t = i / 40;
        const x = -0.82 + t * 1.66;
        const z = 0.15 * Math.sin(Math.PI * Math.pow(t, 0.9)) + 0.02;
        const p: V3 = [x, 0.76, z];
        if (i > 0) {
          const prev = [-0.82 + ((i - 1) / 40) * 1.66, 0.76, 0.15 * Math.sin(Math.PI * Math.pow((i - 1) / 40, 0.9)) + 0.02] as V3;
          pushSeg(lines, prev, p, [0.0, 0.29, 0.99, 0.9]);
        }
      }
    }
  }

  return { lines, fill };
}

/* ------------------------------------------------------------------ */
/* Component                                                          */
/* ------------------------------------------------------------------ */
export default function Viewport3D({ className }: { className?: string }) {
  const cockpit = useCockpit();
  const state = useRef(cockpit);
  state.current = cockpit;

  const host = useRef<HTMLDivElement>(null);
  const glCanvas = useRef<HTMLCanvasElement>(null);
  const overlay = useRef<HTMLCanvasElement>(null);
  const [glError, setGlError] = useState<string | null>(null);
  const [fps, setFps] = useState(120);
  const [hover, setHover] = useState<{ x: number; y: number; name: string; group: number; faces: number } | null>(null);
  const [dropHover, setDropHover] = useState(false);
  const [tool, setTool] = useState("face");
  const [showActors, setShowActors] = useState(true);

  const pickFn = useRef<((x: number, y: number) => void) | null>(null);
  const camRef = cockpit.camera;
  const handleScreens = useRef<{ a: { x: number; y: number }; b: { x: number; y: number } }>({ a: { x: -99, y: -99 }, b: { x: -99, y: -99 } });
  const lastPointer = useRef({ x: 0, y: 0 });
  const drag = useRef<{ mode: "orbit" | "pan" | "a" | "b"; x: number; y: number; moved: number; anchor?: V3 } | null>(null);
  const [legend, setLegend] = useState<{ name: string; min: number; max: number; unit: string; colormap: ColormapName }>({
    name: "Cp", min: -2.2, max: 1.2, unit: "—", colormap: "turbo",
  });

  useEffect(() => {
    const view = camRef.current;
    const preset = cockpit.viewPreset.name;
    const presets: Record<string, { az: number; el: number; d: number; t: V3 }> = {
      "Hero 3/4": { az: -0.72, el: 0.34, d: 6.4, t: [0.12, 0.02, 0.04] },
      "Inlet / A": { az: -1.62, el: 0.16, d: 6.8, t: [0.1, 0.02, 0] },
      "Suction side": { az: 0.04, el: 0.1, d: 5.4, t: [0.1, 0.02, 0] },
      "Top / cascade": { az: 0, el: 1.5, d: 6.4, t: [0.05, 0, 0] },
      "Probe close": { az: -0.95, el: 0.42, d: 4.1, t: [0.05, 0.14, 0.1] },
    };
    const p = presets[preset] || presets["Hero 3/4"];
    camRef.current = { azimuth: p.az, elevation: p.el, distance: p.d, target: p.t };
    if (overlay.current) overlay.current.dataset.preset = preset;
    return () => { camRef.current = view; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [cockpit.viewPreset.n]);

  useEffect(() => {
    const canvas = glCanvas.current;
    const hostEl = host.current;
    const ctx2d = overlay.current?.getContext("2d");
    if (!canvas || !hostEl || !ctx2d) return;
    const gl = canvas.getContext("webgl2", { antialias: true, alpha: true, premultipliedAlpha: true, preserveDrawingBuffer: false });
    if (!gl) {
      setGlError("WebGL2 is required for the R32Uint pick buffer. Enable hardware acceleration to continue.");
      return;
    }

    let blade: Prog, pickProg: Prog, lineProg: Prog;
    try {
      blade = createProgram(gl, BLADE_VS, BLADE_FS);
      pickProg = createProgram(gl, PICK_VS, PICK_FS);
      lineProg = createProgram(gl, LINE_VS, LINE_FS);
    } catch (e) {
      setGlError(String(e));
      return;
    }

    const mesh: MeshData = buildBladeMesh();
    const vao = gl.createVertexArray()!;
    gl.bindVertexArray(vao);
    const buffer = (data: Float32Array | Uint32Array, loc: number, size: number, type: number) => {
      const b = gl.createBuffer()!;
      gl.bindBuffer(gl.ARRAY_BUFFER, b);
      gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
      gl.enableVertexAttribArray(loc);
      gl.vertexAttribPointer(loc, size, type, false, 0, 0);
      return b;
    };
    buffer(mesh.positions, 0, 3, gl.FLOAT);
    buffer(mesh.normals, 1, 3, gl.FLOAT);
    buffer(mesh.groups, 2, 1, gl.FLOAT);
    buffer(mesh.base, 3, 2, gl.FLOAT);
    const valABuf = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, valABuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(mesh.positions.length / 3), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(4);
    gl.vertexAttribPointer(4, 1, gl.FLOAT, false, 0, 0);
    const valBBuf = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, valBBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(mesh.positions.length / 3), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(5);
    gl.vertexAttribPointer(5, 1, gl.FLOAT, false, 0, 0);
    const flagBuf = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, flagBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Uint8Array(mesh.positions.length / 3), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(6);
    gl.vertexAttribPointer(6, 1, gl.UNSIGNED_BYTE, false, 0, 0);
    const ibo = gl.createBuffer()!;
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, mesh.indices, gl.STATIC_DRAW);
    gl.bindVertexArray(null);

    // line VAO
    const lineVAO = gl.createVertexArray()!;
    const linePosBuf = gl.createBuffer()!;
    const lineColBuf = gl.createBuffer()!;
    gl.bindVertexArray(lineVAO);
    gl.bindBuffer(gl.ARRAY_BUFFER, linePosBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, lineColBuf);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);

    const fillVAO = gl.createVertexArray()!;
    const fillPosBuf = gl.createBuffer()!;
    const fillColBuf = gl.createBuffer()!;
    gl.bindVertexArray(fillVAO);
    gl.bindBuffer(gl.ARRAY_BUFFER, fillPosBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, fillColBuf);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);

    const gizmoVAO = gl.createVertexArray()!;
    const gizmoPosBuf = gl.createBuffer()!;
    const gizmoColBuf = gl.createBuffer()!;
    gl.bindVertexArray(gizmoVAO);
    gl.bindBuffer(gl.ARRAY_BUFFER, gizmoPosBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, gizmoColBuf);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);

    const pointVAO = gl.createVertexArray()!;
    const pointPosBuf = gl.createBuffer()!;
    const pointColBuf = gl.createBuffer()!;
    gl.bindVertexArray(pointVAO);
    gl.bindBuffer(gl.ARRAY_BUFFER, pointPosBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, pointColBuf);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);

    // LUT textures
    const lutTex: Record<string, WebGLTexture> = {};
    (["turbo", "coolwarm", "inferno"] as ColormapName[]).forEach((name) => {
      const t = gl.createTexture()!;
      gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 256, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, oklabLUT(name, 256));
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      lutTex[name] = t;
    });

    // pick target (R32Uint)
    let pickFB = gl.createFramebuffer()!;
    let pickTex = gl.createTexture()!;
    let pickDepth = gl.createRenderbuffer()!;
    let pickW = 1, pickH = 1;
    let pickOk = false;
    const allocPick = (w: number, h: number) => {
      pickW = w; pickH = h;
      gl.bindTexture(gl.TEXTURE_2D, pickTex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32UI, w, h, 0, gl.RED_INTEGER, gl.UNSIGNED_INT, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.bindRenderbuffer(gl.RENDERBUFFER, pickDepth);
      gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, w, h);
      gl.bindFramebuffer(gl.FRAMEBUFFER, pickFB);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, pickTex, 0);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, pickDepth);
      gl.drawBuffers([gl.COLOR_ATTACHMENT0]);
      pickOk = gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE;
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    };

    // geometry caches
    const cached = { lines: { pos: new Float32Array(0), col: new Float32Array(0), count: 0 }, fill: { pos: new Float32Array(0), col: new Float32Array(0), count: 0 } };
    let lastDomainJson = "";
    let lastFieldKey = "";
    let lastFlags = "";

    const updateField = (field: FieldKey) => {
      const a = new Float32Array(mesh.positions.length / 3);
      const b = new Float32Array(mesh.positions.length / 3);
      for (let i = 0; i < a.length; i++) {
        const p: V3 = [mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]];
        a[i] = simFieldAt(p, field, "A");
        b[i] = simFieldAt(p, field, "B");
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, valABuf);
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, a);
      gl.bindBuffer(gl.ARRAY_BUFFER, valBBuf);
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, b);
    };

    const updateFlags = () => {
      const flags = new Uint8Array(mesh.positions.length / 3);
      const conflictIds = state.current.conflicts;
      const groups = new Set<number>();
      conflictIds.forEach((pid) => { const p = patchById(pid); if (p.pickable) groups.add(p.group); });
      for (let i = 0; i < flags.length; i++) flags[i] = groups.has(mesh.groups[i]) ? 1 : 0;
      gl.bindBuffer(gl.ARRAY_BUFFER, flagBuf);
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, flags);
    };

    const updateScene = () => {
      const { lines, fill } = buildScene(state.current, state.current.domain);
      gl.bindBuffer(gl.ARRAY_BUFFER, linePosBuf);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(lines.pos), gl.DYNAMIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, lineColBuf);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(lines.col), gl.DYNAMIC_DRAW);
      cached.lines = { pos: new Float32Array(lines.pos), col: new Float32Array(lines.col), count: lines.pos.length / 3 };
      gl.bindBuffer(gl.ARRAY_BUFFER, fillPosBuf);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(fill.pos), gl.DYNAMIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, fillColBuf);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(fill.col), gl.DYNAMIC_DRAW);
      cached.fill = { pos: new Float32Array(fill.pos), col: new Float32Array(fill.col), count: fill.pos.length / 3 };
    };

    let w = 1, h = 1, dpr = Math.min(2, window.devicePixelRatio || 1);
    const resize = () => {
      const r = hostEl.getBoundingClientRect();
      const nw = Math.max(64, Math.floor(r.width * dpr));
      const nh = Math.max(64, Math.floor(r.height * dpr));
      if (nw === w && nh === h) return;
      w = nw; h = nh;
      canvas.width = w; canvas.height = h;
      if (overlay.current) { overlay.current.width = w; overlay.current.height = h; }
      allocPick(Math.max(32, Math.floor(r.width * 0.6)), Math.max(32, Math.floor(r.height * 0.6)));
    };
    resize();
    const ro = new ResizeObserver(resize);
    ro.observe(hostEl);

    const pickAt = (cx: number, cy: number) => {
      if (!pickOk) return;
      const r = hostEl.getBoundingClientRect();
      const px = Math.floor((cx / r.width) * pickW);
      const py = Math.floor(pickH - (cy / r.height) * pickH);
      if (px < 0 || py < 0 || px >= pickW || py >= pickH) return;
      const { vp } = cameraMatrices(camRef.current, pickW, pickH);
      gl.bindFramebuffer(gl.FRAMEBUFFER, pickFB);
      gl.viewport(0, 0, pickW, pickH);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST);
      gl.disable(gl.BLEND);
      gl.useProgram(pickProg.p);
      gl.uniformMatrix4fv(pickProg.u("uVP"), false, vp);
      gl.bindVertexArray(vao);
      gl.drawElements(gl.TRIANGLES, mesh.triCount * 3, gl.UNSIGNED_INT, 0);
      const out = new Uint32Array(1);
      gl.readPixels(px, py, 1, 1, gl.RED_INTEGER, gl.UNSIGNED_INT, out);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      const patch = patchByGroup(out[0]);
      if (patch && patch.pickable) state.current.selectPatch(patch.id);
    };
    pickFn.current = pickAt;

    // ---------------- render ----------------
    let raf = 0;
    let last = performance.now();
    let frames = 0, fpsClock = last;
    let lastHoverPick = 0;

    const drawLines = (data: { pos: Float32Array; col: Float32Array; count: number }, vp: Float32Array, mode: number, pointSize = 4, vaoIn = lineVAO) => {
      if (!data.count) return;
      gl.useProgram(lineProg.p);
      gl.uniformMatrix4fv(lineProg.u("uVP"), false, vp);
      gl.uniform1f(lineProg.u("uPointSize"), pointSize);
      gl.bindVertexArray(vaoIn);
      gl.bindBuffer(gl.ARRAY_BUFFER, gizmoPosBuf);
      gl.bufferData(gl.ARRAY_BUFFER, data.pos, gl.DYNAMIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, gizmoColBuf);
      gl.bufferData(gl.ARRAY_BUFFER, data.col, gl.DYNAMIC_DRAW);
      gl.drawArrays(mode, 0, data.count);
    };

    const drawGizmos = (vp: Float32Array, cssW: number) => {
      const st = state.current;
      if (!st.actorsVisible) return;
      const arr: LineArrays = { pos: [], col: [] };
      const pts: LineArrays = { pos: [], col: [] };
      st.injectors.forEach((inj) => {
        pts.pos.push(...inj.pos); pts.col.push(1, 0.72, 0.16, 1);
        const tip = add(inj.pos, scale(norm(inj.axis), 0.62));
        pts.pos.push(...tip); pts.col.push(1, 0.6, 0.1, 0.55);
      });
      st.mics.forEach((m) => { pts.pos.push(...m.pos); pts.col.push(0.35, 0.95, 0.76, 1); });
      st.porous.forEach((p) => { pts.pos.push(...p.pos); pts.col.push(0.7, 0.6, 1, 1); });
      pts.pos.push(...st.probe.a, ...st.probe.b);
      pts.col.push(0.98, 0.99, 1, 1, 0.98, 0.99, 1, 1);
      arr.pos.push(...st.probe.a, ...st.probe.b);
      arr.col.push(0.85, 0.9, 1, 0.7, 0.85, 0.9, 1, 0.7);
      void cssW;
      drawLines({ pos: new Float32Array(arr.pos), col: new Float32Array(arr.col), count: arr.pos.length / 3 }, vp, gl.LINES, 1, gizmoVAO);
      drawLines({ pos: new Float32Array(pts.pos), col: new Float32Array(pts.col), count: pts.pos.length / 3 }, vp, gl.POINTS, 5.5, pointVAO);
    };

    const renderScene = (vp: Float32Array, viewW: number, viewH: number, mode: number, field: FieldKey, deltaSpan: number, st: typeof state.current) => {
      const f = FIELDS[field];
      const cmap = mode === 2 ? "coolwarm" : COLORMAP_FOR[field] || "turbo";
      gl.useProgram(blade.p);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, lutTex[cmap]);
      gl.uniform1i(blade.u("uLUT"), 0);
      gl.uniformMatrix4fv(blade.u("uVP"), false, vp);
      gl.uniform1i(blade.u("uMode"), mode);
      const customRange = st.activeField === "custom" && st.uploaded ? st.uploaded : null;
      gl.uniform1f(blade.u("uMinA"), customRange ? customRange.min : f.min);
      gl.uniform1f(blade.u("uMaxA"), customRange ? customRange.max : f.max);
      gl.uniform1f(blade.u("uMinB"), customRange ? customRange.min : f.min);
      gl.uniform1f(blade.u("uMaxB"), customRange ? customRange.max : f.max);
      gl.uniform1f(blade.u("uDeltaSpan"), deltaSpan);
      const eye = orbitEye(camRef.current.target, camRef.current.azimuth, camRef.current.elevation, camRef.current.distance);
      gl.uniform3f(blade.u("uEye"), eye[0], eye[1], eye[2]);
      const highlight = st.workspace === "rules" || st.workspace === "domain" ? patchById(st.selectedPatch.id).group : -1;
      gl.uniform1i(blade.u("uHighlight"), mode === 2 ? -1 : highlight);
      const sliceOn = st.workspace === "compare" && st.compare.slice;
      gl.uniform1i(blade.u("uSliceOn"), sliceOn ? 1 : 0);
      gl.uniform3f(blade.u("uSliceNormal"), 1, 0, 0);
      gl.uniform1f(blade.u("uSlicePos"), st.compare.sliceX);
      void viewW; void viewH;
      gl.bindVertexArray(vao);
      gl.disable(gl.CULL_FACE);
      gl.drawElements(gl.TRIANGLES, mesh.triCount * 3, gl.UNSIGNED_INT, 0);
    };

    const render = (now: number) => {
      raf = requestAnimationFrame(render);
      resize();
      const st = state.current;
      const cssW = hostEl.clientWidth, cssH = hostEl.clientHeight;
      dpr = Math.min(2, window.devicePixelRatio || 1);

      const domainKey = JSON.stringify({
        domain: st.domain,
        actors: st.actorsVisible,
        probe: { a: st.probe.a, b: st.probe.b, kind: st.probe.kind },
        injectors: st.injectors,
        mics: st.mics,
        porous: st.porous,
      });
      if (domainKey !== lastDomainJson) { lastDomainJson = domainKey; updateScene(); }
      const field: FieldKey = (st.workspace === "compare" ? st.compare.field : st.activeField === "custom" ? "p" : st.activeField) as FieldKey;
      const fieldKey = `${field}`;
      if (fieldKey !== lastFieldKey) { lastFieldKey = fieldKey; updateField(field); }
      const flagKey = [...st.conflicts].join(",");
      if (flagKey !== lastFlags) { lastFlags = flagKey; updateFlags(); }
      const f = FIELDS[field];
      const deltaSpan = Math.max(0.0001, (f.max - f.min) * 0.1);
      setLegend((prev) => {
        const isCmp = st.workspace === "compare";
        const cust = st.activeField === "custom" && st.uploaded;
        const name = isCmp ? "Δφ" : cust ? st.uploaded!.name : f.sym;
        const cmap = isCmp ? "coolwarm" : (COLORMAP_FOR[field] || "turbo");
        const lmin = isCmp ? -deltaSpan : cust ? st.uploaded!.min : f.min;
        const lmax = isCmp ? deltaSpan : cust ? st.uploaded!.max : f.max;
        const unit = isCmp ? f.unit : cust ? st.uploaded!.unit : f.unit;
        if (prev.name === name && prev.min === lmin && prev.max === lmax && prev.colormap === cmap) return prev;
        return { name, min: lmin, max: lmax, unit, colormap: cmap };
      });

      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, w, h);
      gl.disable(gl.SCISSOR_TEST);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST);
      gl.depthFunc(gl.LEQUAL);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

      const compare = st.workspace === "compare";
      const halves: { x: number; w: number; mode: number; split: boolean }[] = [];
      if (compare && st.compare.mode === "split") {
        halves.push({ x: 0, w: w / 2, mode: 0, split: true }, { x: w / 2, w: w / 2, mode: 1, split: true });
      } else if (compare && st.compare.mode === "wipe") {
        halves.push({ x: 0, w: Math.floor(w * st.compare.wipe), mode: 0, split: true }, { x: Math.floor(w * st.compare.wipe), w: w - Math.floor(w * st.compare.wipe), mode: 2, split: true });
      } else {
        halves.push({ x: 0, w, mode: compare && st.compare.mode === "delta" ? 2 : 0, split: false });
      }

      const fullVp = cameraMatrices(camRef.current, cssW, cssH).vp;

      halves.forEach((half) => {
        if (st.compare.mode === "wipe") {
          gl.viewport(0, 0, w, h);
          gl.enable(gl.SCISSOR_TEST);
          gl.scissor(half.x, 0, half.w, h);
        } else if (st.compare.mode === "split") {
          gl.viewport(half.x, 0, half.w, h);
        } else {
          gl.viewport(0, 0, w, h);
        }
        const { vp } = half.split && st.compare.mode === "split"
          ? cameraMatrices(camRef.current, Math.max(1, cssW / 2), cssH)
          : { vp: fullVp };

        // translucent MRF shell
        if (st.domain.showMrf) {
          gl.useProgram(lineProg.p);
          gl.uniformMatrix4fv(lineProg.u("uVP"), false, vp);
          gl.uniform1f(lineProg.u("uPointSize"), 1);
          gl.bindVertexArray(fillVAO);
          gl.bindBuffer(gl.ARRAY_BUFFER, fillPosBuf);
          gl.bufferData(gl.ARRAY_BUFFER, cached.fill.pos, gl.DYNAMIC_DRAW);
          gl.bindBuffer(gl.ARRAY_BUFFER, fillColBuf);
          gl.bufferData(gl.ARRAY_BUFFER, cached.fill.col, gl.DYNAMIC_DRAW);
          gl.depthMask(false);
          gl.drawArrays(gl.TRIANGLES, 0, cached.fill.count);
          gl.depthMask(true);
        }

        renderScene(vp, cssW, cssH, half.mode, field, deltaSpan, st);

        gl.useProgram(lineProg.p);
        gl.uniformMatrix4fv(lineProg.u("uVP"), false, vp);
        gl.uniform1f(lineProg.u("uPointSize"), 4);
        gl.bindVertexArray(lineVAO);
        gl.bindBuffer(gl.ARRAY_BUFFER, linePosBuf);
        gl.bufferData(gl.ARRAY_BUFFER, cached.lines.pos, gl.DYNAMIC_DRAW);
        gl.bindBuffer(gl.ARRAY_BUFFER, lineColBuf);
        gl.bufferData(gl.ARRAY_BUFFER, cached.lines.col, gl.DYNAMIC_DRAW);
        gl.drawArrays(gl.LINES, 0, cached.lines.count);

        drawGizmos(vp, cssW);
      });

      gl.disable(gl.SCISSOR_TEST);

      // ---------------- 2D overlay ----------------
      const ctx = ctx2d;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, cssW, cssH);
      const { vp } = cameraMatrices(camRef.current, cssW, cssH);
      const proj = (p: V3) => projectToScreen(p, vp, cssW, cssH);

      const pill = (x: number, y: number, text: string, color = "#dbe0ea", bg = "rgba(14,15,19,0.82)", border = "#31333c", anchor: "start" | "center" = "start", font = "9.5px") => {
        ctx.font = `500 ${font} "JetBrains Mono", monospace`;
        ctx.textAlign = "left";
        const tw = ctx.measureText(text).width;
        const px = anchor === "center" ? x - tw / 2 : x;
        ctx.beginPath();
        ctx.roundRect(px - 5, y - 11, tw + 10, 17, 3);
        ctx.fillStyle = bg;
        ctx.fill();
        ctx.strokeStyle = border;
        ctx.lineWidth = 1;
        ctx.stroke();
        ctx.fillStyle = color;
        ctx.fillText(text, px, y + 1);
        return tw + 10;
      };

      // selected patch label
      if (st.workspace === "rules" || st.workspace === "domain") {
        const patch = patchById(st.selectedPatch.id);
        const s = proj(patch.centroid);
        if (s.visible) {
          const conflict = st.conflicts.has(patch.id);
          const col = conflict ? "#EF233C" : "#004AFD";
          ctx.beginPath();
          ctx.arc(s.x, s.y, 4, 0, Math.PI * 2);
          ctx.fillStyle = col;
          ctx.fill();
          ctx.strokeStyle = "rgba(255,255,255,0.9)";
          ctx.lineWidth = 1.2;
          ctx.stroke();
          ctx.beginPath();
          ctx.moveTo(s.x, s.y);
          ctx.lineTo(s.x + 34, s.y - 34);
          ctx.strokeStyle = conflict ? "rgba(239,35,60,0.65)" : "rgba(77,124,255,0.65)";
          ctx.stroke();
          pill(s.x + 38, s.y - 41, `${patch.short} · ${conflict ? "CONFLICT" : `${patch.faces} faces`}`, conflict ? "#ff8798" : "#9fbcff", "rgba(12,13,17,0.9)", conflict ? "#EF233C66" : "#004AFD66");
        }
      }

      // domain + mrf labels
      if (st.workspace !== "compare" && st.domain.showDomain) {
        const x0 = -1.05 - st.domain.upstream * 0.2;
        const x1 = 0.92 + st.domain.downstream * 0.065;
        const rad = st.domain.radial * 0.36;
        const up = proj([x0, 0.98, rad]);
        const down = proj([x1, 0.98, rad]);
        if (up.visible) pill(up.x, up.y - 4, `INLET · ${st.domain.upstream.toFixed(0)}L`, "#8cecff", "rgba(11,19,23,0.85)", "#24505c");
        if (down.visible) pill(down.x - 20, down.y - 4, `OUTLET · ${st.domain.downstream.toFixed(0)}L`, "#8cecff", "rgba(11,19,23,0.85)", "#24505c");
      }
      if (st.workspace !== "compare" && st.domain.showMrf) {
        const p = proj([0, 0.18 + st.domain.mrfHeight / 2 + 0.36, 0]);
        if (p.visible) pill(p.x, p.y, `MRF · ${st.domain.rpm.toLocaleString()} rpm · tip Ma ${(st.tipMach).toFixed(3)}`, "#8cecff", "rgba(8,17,22,0.86)", "#24505c", "center");
      }

      if (st.actorsVisible && st.workspace !== "compare") {
        st.injectors.forEach((inj) => {
          const p = proj(add(inj.pos, scale(norm(inj.axis), 0.62)));
          if (p.visible) pill(p.x + 8, p.y, `DPM ${inj.id} · θ${inj.cone}° · d₃₂ ${inj.d32}µm`, "#ffcb6b", "rgba(24,18,10,0.88)", "#5a4319");
        });
        st.mics.forEach((m) => {
          const p = proj(m.pos);
          if (p.visible) {
            pill(p.x + 10, p.y, `FWH ${m.id} · r ${m.radius.toFixed(2)} m`, "#8cecff", "rgba(10,21,22,0.86)", "#244b50");
            ctx.beginPath(); ctx.arc(p.x, p.y, 3.2, 0, Math.PI * 2); ctx.fillStyle = "#65e5b1"; ctx.fill();
          }
        });
        st.porous.forEach((pz) => {
          const p = proj(pz.pos);
          if (p.visible) pill(p.x + 10, p.y - 12, `POM ${pz.id} · e₁e₂e₃ κ 2.4e-9`, "#c3b2ff", "rgba(18,14,26,0.88)", "#3f3560");
        });
      }

      // probe handles + labels
      if (st.workspace === "probe" || st.workspace === "rules") {
        const A = proj(st.probe.a);
        const B = proj(st.probe.b);
        handleScreens.current = { a: { x: A.x, y: A.y }, b: { x: B.x, y: B.y } };
        if (A.visible) {
          ctx.beginPath(); ctx.arc(A.x, A.y, 5.5, 0, Math.PI * 2);
          ctx.fillStyle = "rgba(14,15,19,0.9)"; ctx.fill();
          ctx.strokeStyle = "#9fbcff"; ctx.lineWidth = 1.8; ctx.stroke();
          pill(A.x + 9, A.y + 4, "pA", "#cfe0ff", "rgba(10,13,22,0.85)", "#3b4f7a");
        }
        if (B.visible) {
          ctx.beginPath(); ctx.arc(B.x, B.y, 5.5, 0, Math.PI * 2);
          ctx.fillStyle = "rgba(14,15,19,0.9)"; ctx.fill();
          ctx.strokeStyle = "#9fbcff"; ctx.lineWidth = 1.8; ctx.stroke();
          pill(B.x + 9, B.y + 4, "pB", "#cfe0ff", "rgba(10,13,22,0.85)", "#3b4f7a");
        }
        const mid = proj(scale(add(st.probe.a, st.probe.b), 0.5));
        if (mid.visible) {
          const L = Math.hypot(st.probe.b[0] - st.probe.a[0], st.probe.b[1] - st.probe.a[1], st.probe.b[2] - st.probe.a[2]);
          pill(mid.x, mid.y - 12, `rake ${L.toFixed(3)} m · ${st.probe.samples} samples`, "#b9c3d8", "rgba(13,14,18,0.88)", "#333540", "center");
        }
      }

      // axis triad
      const ax = 66, ay = cssH - 48;
      const axisDir = (v: V3): [number, number] => {
        const eye = add(camRef.current.target, [0, 0, 0]);
        void eye;
        const m = cameraMatrices(camRef.current, cssW, cssH).view;
        const x = m[0] * v[0] + m[4] * v[1] + m[8] * v[2];
        const y = m[1] * v[0] + m[5] * v[1] + m[9] * v[2];
        return [x, -y];
      };
      const axes: [V3, string, readonly number[]][] = [
        [[1, 0, 0], "X", AXIS_COLORS.x], [[0, 1, 0], "Y", AXIS_COLORS.y], [[0, 0, 1], "Z", AXIS_COLORS.z],
      ];
      ctx.save();
      ctx.translate(ax, ay);
      axes.sort((a, b) => axisDir(a[0])[1] - axisDir(b[0])[1]);
      axes.forEach(([v, label, col]) => {
        const [dx, dy] = axisDir(v);
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(dx * 21, dy * 21);
        ctx.strokeStyle = `rgba(${col.map((c) => Math.round(c * 255)).join(",")})`;
        ctx.lineWidth = 1.4;
        ctx.stroke();
        ctx.fillStyle = `rgb(${col.map((c) => Math.round(c * 255)).join(",")})`;
        ctx.font = '600 8px "JetBrains Mono", monospace';
        ctx.textAlign = "center";
        ctx.fillText(label, dx * 27, dy * 27 + 3);
      });
      ctx.beginPath(); ctx.arc(0, 0, 2, 0, Math.PI * 2); ctx.fillStyle = "#7d8598"; ctx.fill();
      ctx.restore();

      // compare overlays
      if (compare) {
        const cmp = st.compare;
        if (cmp.mode === "wipe") {
          const x = cssW * cmp.wipe;
          ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, cssH);
          ctx.strokeStyle = "rgba(255,255,255,0.55)"; ctx.lineWidth = 1; ctx.setLineDash([5, 4]); ctx.stroke(); ctx.setLineDash([]);
          ctx.beginPath(); ctx.arc(x, cssH / 2, 8, 0, Math.PI * 2);
          ctx.fillStyle = "rgba(13,14,19,0.92)"; ctx.fill(); ctx.strokeStyle = "rgba(255,255,255,0.5)"; ctx.stroke();
          ctx.fillStyle = "#d9dee9"; ctx.font = '9px "JetBrains Mono"'; ctx.textAlign = "center"; ctx.fillText("⇔", x, cssH / 2 + 3);
          pill(14, 54, "A · BASELINE", "#dbe0ea", "rgba(13,14,19,0.8)", "#3a3c46");
          pill(x + 14, 54, "Δφ · B − A", "#ffcdbc", "rgba(25,14,14,0.82)", "#6a3931");
        } else if (cmp.mode === "split") {
          ctx.beginPath(); ctx.moveTo(cssW / 2, 0); ctx.lineTo(cssW / 2, cssH);
          ctx.strokeStyle = "rgba(255,255,255,0.45)"; ctx.lineWidth = 1; ctx.stroke();
          pill(14, cssH - 20, "RUN A · BASELINE", "#dbe0ea", "rgba(13,14,19,0.82)", "#3a3c46");
          pill(cssW / 2 + 14, cssH - 20, "RUN B · ADJOINT #37", "#bcd0ff", "rgba(12,16,28,0.85)", "#35446b");
        } else {
          pill(14, 54, "DIFFERENTIAL FIELD · Δφ = φB − φA", "#ffcdbc", "rgba(25,14,14,0.82)", "#6a3931");
        }
      }

      // hover tooltip
      if (hover && !drag.current) {
        pill(hover.x + 14, hover.y - 8, `${hover.name} · group ${hover.group} · ${hover.faces} faces`, "#dbe0ea", "rgba(10,11,15,0.94)", "#4a4d5c");
        pill(hover.x + 14, hover.y + 10, "LMB synthesize spatial rule", "#9fbcff", "rgba(8,12,24,0.92)", "#2f3f63", "start", "8.5px");
      }

      // fps + camera hud
      ctx.textAlign = "right";
      ctx.font = '9px "JetBrains Mono", monospace';
      ctx.fillStyle = "rgba(160,168,183,0.85)";
      ctx.fillText(`${fps} FPS · orbit ${(camRef.current.azimuth * 180 / Math.PI).toFixed(0)}° / ${(camRef.current.elevation * 180 / Math.PI).toFixed(0)}° · dist ${camRef.current.distance.toFixed(2)} m`, cssW - 12, cssH - 10);
      ctx.textAlign = "left";
      ctx.fillStyle = "rgba(140,148,164,0.8)";
      ctx.fillText("LMB orbit · MMB/RMB pan · wheel zoom · click face to author", 12, cssH - 10);

      frames++;
      if (now - fpsClock > 700) {
        setFps(Math.round((frames * 1000) / (now - fpsClock)));
        frames = 0; fpsClock = now;
      }

      // hover picking at 7 Hz
      const p = lastPointer.current;
      const inBounds = p.x >= 0 && p.y >= 0 && p.x <= cssW && p.y <= cssH;
      if (inBounds && now - lastHoverPick > 140 && !drag.current) {
        lastHoverPick = now;
        const r = hostEl.getBoundingClientRect();
        if (!pickOk) return;
        const px = Math.floor((p.x / r.width) * pickW);
        const py = Math.floor(pickH - (p.y / r.height) * pickH);
        const { vp: pvp } = cameraMatrices(camRef.current, pickW, pickH);
        gl.bindFramebuffer(gl.FRAMEBUFFER, pickFB);
        gl.viewport(0, 0, pickW, pickH);
        gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.disable(gl.BLEND);
        gl.useProgram(pickProg.p);
        gl.uniformMatrix4fv(pickProg.u("uVP"), false, pvp);
        gl.bindVertexArray(vao);
        gl.drawElements(gl.TRIANGLES, mesh.triCount * 3, gl.UNSIGNED_INT, 0);
        const out = new Uint32Array(1);
        if (px >= 0 && py >= 0 && px < pickW && py < pickH) gl.readPixels(px, py, 1, 1, gl.RED_INTEGER, gl.UNSIGNED_INT, out);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.enable(gl.BLEND);
        const patch = patchByGroup(out[0]);
        if (patch && patch.pickable) {
          setHover({ x: p.x, y: p.y, name: patch.short, group: patch.group, faces: patch.faces });
          if (state.current.hoveredPatch !== patch.id) state.current.setHoveredPatch(patch.id);
        } else {
          setHover(null);
          if (state.current.hoveredPatch) state.current.setHoveredPatch(null);
        }
      }
    };
    raf = requestAnimationFrame(render);

    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const c = camRef.current;
      c.distance = Math.min(24, Math.max(1.9, c.distance * Math.exp(e.deltaY * 0.0011)));
    };
    hostEl.addEventListener("wheel", onWheel, { passive: false });

    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
      hostEl.removeEventListener("wheel", onWheel);
      gl.deleteFramebuffer(pickFB);
      gl.deleteTexture(pickTex);
      gl.deleteRenderbuffer(pickDepth);
      pickFn.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const onPointerDown = (e: React.PointerEvent) => {
    const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
    const x = e.clientX - r.left, y = e.clientY - r.top;
    lastPointer.current = { x, y };
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
    const da = Math.hypot(x - handleScreens.current.a.x, y - handleScreens.current.a.y);
    const db = Math.hypot(x - handleScreens.current.b.x, y - handleScreens.current.b.y);
    if (e.button === 0 && (da < 12 || db < 12)) {
      drag.current = { mode: da < db ? "a" : "b", x, y, moved: 0 };
      return;
    }
    drag.current = { mode: e.button === 0 && !e.shiftKey ? "orbit" : "pan", x, y, moved: 0 };
  };

  const onPointerMove = (e: React.PointerEvent) => {
    const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
    const x = e.clientX - r.left, y = e.clientY - r.top;
    lastPointer.current = { x, y };
    const d = drag.current;
    if (!d) { if (host.current) host.current.style.cursor = "crosshair"; return; }
    const dx = x - d.x, dy = y - d.y;
    d.moved += Math.abs(dx) + Math.abs(dy);
    d.x = x; d.y = y;
    if (d.mode === "orbit") {
      camRef.current.azimuth -= dx * 0.0072;
      camRef.current.elevation = Math.min(1.48, Math.max(-0.28, camRef.current.elevation + dy * 0.006));
      if (host.current) host.current.style.cursor = "grabbing";
    } else if (d.mode === "pan") {
      const cam = camRef.current;
      const forward = norm(sub(cam.target, add(cam.target, [0, 0, 0]))); void forward;
      const az = cam.azimuth;
      const right: V3 = [Math.cos(az), 0, -Math.sin(az)];
      const up: V3 = [0, 1, 0];
      const k = cam.distance * 0.0016;
      cam.target = add(cam.target, add(scale(right, -dx * k), scale(up, dy * k)));
    } else {
      // drag rake endpoint on the camera-facing plane through the current point
      const cam = camRef.current;
      const eye = [cam.target[0] + cam.distance * Math.cos(cam.elevation) * Math.sin(cam.azimuth), cam.target[1] + cam.distance * Math.sin(cam.elevation), cam.target[2] + cam.distance * Math.cos(cam.elevation) * Math.cos(cam.azimuth)] as V3;
      const n = norm(sub(cam.target, eye));
      const anchor = d.mode === "a" ? state.current.probe.a : state.current.probe.b;
      const ray = screenRay(x, y, r.width, r.height);
      const denom = dot(ray.dir, n);
      if (Math.abs(denom) > 1e-5) {
        const t = dot(sub(anchor, ray.origin), n) / denom;
        const world = add(ray.origin, scale(ray.dir, t));
        state.current.setProbe((p) => ({ ...p, [d.mode === "a" ? "a" : "b"]: [Number(world[0].toFixed(3)), Number(world[1].toFixed(3)), Number(world[2].toFixed(3))] }));
      }
    }
  };

  const screenRay = (x: number, y: number, wpx: number, hpx: number): { origin: V3; dir: V3 } => {
    const cam = camRef.current;
    const { vp } = cameraMatrices(cam, wpx, hpx);
    const inv = invert4(vp);
    const nx = (x / wpx) * 2 - 1, ny = 1 - (y / hpx) * 2;
    const near = mulPoint(inv, [nx, ny, -1]);
    const far = mulPoint(inv, [nx, ny, 1]);
    return { origin: near, dir: norm(sub(far, near)) };
  };

  const onPointerUp = (e: React.PointerEvent) => {
    const d = drag.current;
    drag.current = null;
    if (host.current) host.current.style.cursor = "crosshair";
    try { (e.currentTarget as HTMLElement).releasePointerCapture(e.pointerId); } catch { /* noop */ }
    if (!d) return;
    if (d.mode === "orbit" && d.moved < 4) {
      const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
      pickFn.current?.(e.clientX - r.left, e.clientY - r.top);
    }
  };

  const onDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setDropHover(false);
    const kind = e.dataTransfer.getData("text/actor") as ActorKind;
    if (!kind) return;
    const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
    const ray = screenRay(e.clientX - r.left, e.clientY - r.top, r.width, r.height);
    const n: V3 = [0.35, 0.2, 0.55];
    const denom = dot(ray.dir, n);
    let pos: V3 = [-0.9, 0.3, 0.5];
    if (Math.abs(denom) > 1e-5) {
      const t = dot(sub([0, 0.1, 0], ray.origin), n) / denom;
      const world = add(ray.origin, scale(ray.dir, t));
      pos = [Number(world[0].toFixed(2)), Number(world[1].toFixed(2)), Number(world[2].toFixed(2))];
    }
    cockpit.addActor(kind, pos);
  };

  const legendText = cockpit.activeField === "custom" && cockpit.uploaded ? cockpit.uploaded.name : legend.name;

  return (
    <div
      ref={host}
      className={cn("relative h-full w-full select-none overflow-hidden bg-[#0f1014]", dropHover && "ring-2 ring-inset ring-[#004AFD]", className)}
      style={{ background: "radial-gradient(1200px 620px at 55% 30%, #1b1e26 0%, #14161c 38%, #0c0d11 100%)" }}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      onDragOver={(e) => { e.preventDefault(); setDropHover(true); }}
      onDragLeave={() => setDropHover(false)}
      onDrop={onDrop}
    >
      <canvas ref={glCanvas} className="absolute inset-0 h-full w-full" style={{ touchAction: "none" }} />
      <canvas ref={overlay} className="pointer-events-none absolute inset-0 h-full w-full" />

      {glError && (
        <div className="absolute inset-0 flex items-center justify-center bg-[#0c0d11]/95 p-8">
          <div className="panel max-w-sm p-5 text-center">
            <TriangleAlert size={22} className="mx-auto mb-2 text-[#EF233C]" />
            <div className="mb-1 text-sm font-medium">WebGL2 unavailable</div>
            <p className="text-[11px] leading-relaxed text-[#8b91a0]">{glError}</p>
          </div>
        </div>
      )}

      {/* viewport header */}
      <div className="pointer-events-none absolute left-0 right-0 top-0 flex items-start justify-between gap-2 p-2.5">
        <div className="pointer-events-auto flex flex-col gap-1.5">
          <div className="flex items-center gap-1.5">
            <Chip tone="green" dot="#23D18B">GPU live</Chip>
            <Chip tone="neutral">WGSL · reverse-Z</Chip>
            <Chip tone="blue">R32Uint pick</Chip>
            <Chip tone="neutral">{cockpit.workspace === "compare" ? "SYNC ORBIT ×2" : "4.19M faces"}</Chip>
          </div>
          <div className="flex items-center gap-2 text-[9.5px] text-[#6f7583]">
            <span className="mono text-[#9aa1af]">STATOR ⟶ ROTOR · STAGE 2</span>
            <span className="h-[9px] w-px bg-[#33343c]" />
            <span>CFDGEO01 · CFDMSH02 · CFDFLD03</span>
          </div>
        </div>

        <div className="pointer-events-auto flex items-center gap-1.5">
          <div className="flex items-center gap-0.5 rounded-[5px] border border-[#2b2c34] bg-[#121318]/95 p-0.5 backdrop-blur">
            <IconBtn title="Toggle domain box" active={cockpit.domain.showDomain} onClick={() => cockpit.setDomain((d) => ({ ...d, showDomain: !d.showDomain }))}><Box size={13} /></IconBtn>
            <IconBtn title="Toggle MRF zone" active={cockpit.domain.showMrf} onClick={() => cockpit.setDomain((d) => ({ ...d, showMrf: !d.showMrf }))}><Gauge size={13} /></IconBtn>
            <IconBtn title="Toggle mesh sections" active={cockpit.domain.showMesh} onClick={() => cockpit.setDomain((d) => ({ ...d, showMesh: !d.showMesh }))}><Grid3x3 size={13} /></IconBtn>
            <IconBtn title="Toggle physics actors" active={cockpit.actorsVisible} onClick={() => cockpit.setActorsVisible(!cockpit.actorsVisible)}><Sparkles size={13} /></IconBtn>
            <IconBtn title="Slice plane" active={cockpit.workspace === "compare" && cockpit.compare.slice} onClick={() => cockpit.setCompare((c) => ({ ...c, slice: !c.slice }))}><Scissors size={13} /></IconBtn>
          </div>
          <div className="flex h-[26px] items-center gap-1 rounded-[5px] border border-[#2b2c34] bg-[#121318]/95 px-1 backdrop-blur">
            <Orbit size={12} className="ml-0.5 text-[#6d7381]" />
            <select
              value={cockpit.viewPreset.name}
              onChange={(e) => cockpit.setViewPreset(e.target.value)}
              className="h-[22px] border-0 bg-transparent pr-1 text-[10px] text-[#c2c7d2] outline-none"
            >
              {["Hero 3/4", "Inlet / A", "Suction side", "Top / cascade", "Probe close"].map((v) => <option key={v} value={v} className="bg-[#17181d]">{v}</option>)}
            </select>
          </div>
          <IconBtn title="Fit view" onClick={() => cockpit.setViewPreset("Hero 3/4")}><Focus size={13} /></IconBtn>
          <IconBtn title="Hide UI panels" active={cockpit.hideUi} onClick={() => cockpit.setHideUi(!cockpit.hideUi)}><Eye size={13} /></IconBtn>
          <IconBtn title="Fullscreen viewport" onClick={() => {
            const el = host.current;
            if (!el) return;
            if (document.fullscreenElement) document.exitFullscreen();
            else el.requestFullscreen?.();
          }}><Maximize2 size={13} /></IconBtn>
        </div>
      </div>

      {/* tool rail */}
      <div className="absolute bottom-8 left-2.5 flex flex-col gap-0.5 rounded-[6px] border border-[#2b2c34] bg-[#121318]/92 p-0.5 backdrop-blur">
        {[
          { id: "face", icon: <MousePointer2 size={14} />, title: "Face pick (R32Uint)" },
          { id: "orbit", icon: <Orbit size={14} />, title: "Orbit camera" },
          { id: "rule", icon: <Wrench size={14} />, title: "Spatial rule builder" },
          { id: "probe", icon: <Ruler size={14} />, title: "Line probe / rake" },
          { id: "measure", icon: <Ruler size={14} />, title: "Measure" },
        ].map((t) => (
          <IconBtn
            key={t.id}
            title={t.title}
            active={tool === t.id}
            onClick={() => {
              setTool(t.id);
              if (t.id === "probe") cockpit.setWorkspace("probe");
              if (t.id === "rule") cockpit.setWorkspace("rules");
            }}
          >
            {t.icon}
          </IconBtn>
        ))}
        <div className="my-0.5 h-px bg-[#2b2c34]" />
        <IconBtn title={showActors ? "Hide gizmo labels" : "Show gizmo labels"} active={showActors} onClick={() => setShowActors(!showActors)}><Layers size={14} /></IconBtn>
        <IconBtn title="Drop actors from the right panel" onClick={() => cockpit.notify("Drag a physics actor from the Domain & Actors palette into the viewport")}><Move3d size={14} /></IconBtn>
      </div>

      {/* bottom center hint */}
      <div className="pointer-events-none absolute bottom-0 left-0 right-0 flex justify-center pb-1.5">
        <div className="mono flex items-center gap-2 rounded-t-[4px] border border-b-0 border-[#2a2b33] bg-[#101116]/85 px-2 py-[2px] text-[8.5px] text-[#6b7180] backdrop-blur">
          <span className="flex items-center gap-1"><Target size={9} /> pick → synthesize</span>
          <span className="h-[8px] w-px bg-[#33343d]" />
          <span>trilinear probe · SI-checked upload</span>
          <span className="h-[8px] w-px bg-[#33343d]" />
          <span className="text-[#8fa9e8]">{cockpit.hoveredPatch ? patchById(cockpit.hoveredPatch).short : "hover surface"}</span>
        </div>
      </div>

      {/* legend */}
      <div className="pointer-events-none absolute bottom-10 right-2.5 w-[178px] rounded-[5px] border border-[#2b2c34] bg-[#111216]/92 p-2 backdrop-blur">
        <div className="mb-1 flex items-center justify-between">
          <span className="mono text-[9.5px] text-[#d5dae5]">{legendText}</span>
          <span className="mono text-[8px] text-[#6d7381]">{cockpit.workspace === "compare" ? "Δ Oklab" : "Oklab LUT"}</span>
        </div>
        <div className="h-[6px] w-full rounded-[1px]" style={{ background: gradientCss(legend.colormap) }} />
        <div className="mt-1 flex justify-between">
          <span className="mono text-[8px] tabular text-[#6d7381]">{legend.min.toFixed(legend.name === "p" ? 0 : 2)}</span>
          <span className="mono text-[8px] text-[#6d7381]">{legend.unit}</span>
          <span className="mono text-[8px] tabular text-[#6d7381]">{legend.max.toFixed(legend.name === "p" ? 0 : 2)}</span>
        </div>
        <div className="mt-1 flex items-center justify-between border-t border-[#24252c] pt-1">
          <span className="flex items-center gap-1 text-[8px] text-[#6d7381]"><span className="h-[6px] w-[6px] rounded-[1px] bg-[#004AFD]" />matched</span>
          <span className="flex items-center gap-1 text-[8px] text-[#6d7381]"><span className="h-[6px] w-[6px] rounded-[1px] bg-[#EF233C]" />overlap</span>
          <span className="flex items-center gap-1 text-[8px] text-[#6d7381]"><span className="h-[6px] w-[6px] rounded-[1px] bg-[#FFB020]" />actor</span>
        </div>
      </div>

      {dropHover && (
        <div className="pointer-events-none absolute inset-6 flex items-center justify-center rounded-lg border-2 border-dashed border-[#004AFD]/70 bg-[#004AFD]/[0.04]">
          <div className="rounded-md border border-[#004AFD]/50 bg-[#0d1220]/95 px-4 py-3 text-center">
            <Sparkles size={18} className="mx-auto mb-1 text-[#9fbcff]" />
            <div className="text-xs font-medium text-[#dbe4ff]">Drop physics actor into the domain</div>
            <div className="mono mt-0.5 text-[9px] text-[#7f8ba6]">apex snapped to nearest surface · local frame aligned</div>
          </div>
        </div>
      )}
    </div>
  );
}
