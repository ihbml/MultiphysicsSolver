// ─────────────────────────────────────────────────────────────────────────────
// Two-phase WebGL2 rendering engine (web mirror of rust/src/render/pipeline.rs)
//
//  Phase 1 — geometry → MRT buffers: [albedo | normal | face_group_id] + depth
//  Phase 2 — full-screen compositor: procedural MatCap, screen-space EDL /
//            cavity, selection & hover highlight; unlit passthrough in Mode 2
//  Camera-relative translation (uTr) + reverse-Z infinite projection
//  (depth 1.0 @ near → 0.5 @ far, CompareFunction::GreaterEqual) so a 1 µm
//  boundary layer on a 10 m domain never z-fights.
// ─────────────────────────────────────────────────────────────────────────────
import { useStore } from "../state/store";
import type { Metric } from "../state/store";
import { geoCache } from "../geom/cache";
import {
  VS_STD, FS_SOLID, FS_FIELD, VS_LINE, FS_LINE, VS_VCOL, FS_VCOL,
  VS_MESH, FS_MESH, VS_SLICE, FS_SLICE, VS_SCR, FS_SCR, VS_TRIAD, FS_TRIAD,
} from "./shaders";
import { buildLUT } from "../fields/colormaps";
import {
  FIELD_DEFS, devFactor, integrateStreamline, seedStreamlines,
} from "../fields/analytics";

const METRICS: Metric[] = ["skew", "nonorth", "aspect", "vol"];
const BG: [number, number, number] = [0.055, 0.06, 0.075];
const RED: [number, number, number] = [0.937, 0.137, 0.235];
const PINK: [number, number, number] = [0.988, 0.459, 0.706];
const LIME: [number, number, number] = [0.725, 0.961, 0.0];

type Mat4 = Float32Array;

function mul(a: Mat4, b: Mat4): Mat4 {
  const o = new Float32Array(16);
  for (let c = 0; c < 4; c++)
    for (let r = 0; r < 4; r++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
      o[c * 4 + r] = s;
    }
  return o;
}
function perspRevZ(fovy: number, aspect: number, near: number): Mat4 {
  const t = 1 / Math.tan(fovy / 2);
  const m = new Float32Array(16);
  m[0] = t / aspect;
  m[5] = t;
  m[10] = 0;
  m[11] = -1;
  m[14] = near;
  m[15] = 1;
  return m;
}
function lookAt(eye: number[], target: number[], up: number[]): Mat4 {
  const zx = eye[0] - target[0], zy = eye[1] - target[1], zz = eye[2] - target[2];
  let zl = Math.hypot(zx, zy, zz) || 1;
  const z = [zx / zl, zy / zl, zz / zl];
  const x = [up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]];
  const xl = Math.hypot(x[0], x[1], x[2]) || 1;
  x[0] /= xl; x[1] /= xl; x[2] /= xl;
  const y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]];
  const m = new Float32Array(16);
  m[0] = x[0]; m[1] = y[0]; m[2] = z[0];
  m[4] = x[1]; m[5] = y[1]; m[6] = z[1];
  m[8] = x[2]; m[9] = y[2]; m[10] = z[2];
  m[12] = -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]);
  m[13] = -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]);
  m[14] = -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]);
  m[15] = 1;
  return m;
}

interface Prog {
  p: WebGLProgram;
  uni: Map<string, WebGLUniformLocation | null>;
  attr: Record<string, number>;
}
function mkProg(gl: WebGL2RenderingContext, vs: string, fs: string, attrs: string[]): Prog {
  const c = (type: number, src: string) => {
    const sh = gl.createShader(type)!;
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      console.error("SHADER ERROR", gl.getShaderInfoLog(sh), src.slice(0, 400));
    }
    return sh;
  };
  const p = gl.createProgram()!;
  gl.attachShader(p, c(gl.VERTEX_SHADER, vs));
  gl.attachShader(p, c(gl.FRAGMENT_SHADER, fs));
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) console.error("LINK ERROR", gl.getProgramInfoLog(p));
  const prog: Prog = { p, uni: new Map(), attr: {} };
  for (const a of attrs) prog.attr[a] = gl.getAttribLocation(p, a);
  return prog;
}

export class Engine {
  gl: WebGL2RenderingContext;
  canvas: HTMLCanvasElement;
  raf = 0;
  W = 0; H = 0;
  // camera
  target: [number, number, number] = [5, 1, 0];
  dist = 12.5; yaw = 0.68; pitch = 0.32;
  // resources
  fbo: WebGLFramebuffer | null = null;
  texAlb: WebGLTexture | null = null;
  texNrm: WebGLTexture | null = null;
  texId: WebGLTexture | null = null;
  dep: WebGLRenderbuffer | null = null;
  lut: WebGLTexture | null = null;
  lutData: Uint8Array = buildLUT("viridis", 0);
  lutKey = "";
  progs: Record<string, Prog> = {};
  vaoSolid: WebGLVertexArrayObject | null = null;
  solidCount = 0;
  vaoMesh: (WebGLVertexArrayObject | null)[] = [null, null, null, null];
  meshCount = 0;
  vaoWire: WebGLVertexArrayObject | null = null;
  wireCount = 0;
  vaoOpen: WebGLVertexArrayObject | null = null;
  openCount = 0;
  vaoNon: WebGLVertexArrayObject | null = null;
  nonCount = 0;
  vaoStream: WebGLVertexArrayObject | null = null;
  streamCount = 0;
  vaoSlice: WebGLVertexArrayObject | null = null;
  vaoGizmo: WebGLVertexArrayObject | null = null;
  vaoScr: WebGLVertexArrayObject | null = null;
  vaoTriad: WebGLVertexArrayObject | null = null;
  builtVer = -1;
  streamKey = "";
  // stats
  stats = { fps: 0, draws: 0, tris: 0 };
  private lastT = 0;
  private fpsEma = 60;
  // input
  private dragging = false;
  private dragBtn = -1;
  private dragSlice = false;
  private lastX = 0; lastY = 0;
  private downX = 0; downY = 0;
  private mouseX = -1; mouseY = -1; mouseMoved = false;
  private lastHover = 0;
  private handleScreen: [number, number] = [-100, -100];
  private viewM: Mat4 = new Float32Array(16);

  constructor(canvas: HTMLCanvasElement) {
    this.canvas = canvas;
    const gl = canvas.getContext("webgl2", { antialias: true, alpha: false, preserveDrawingBuffer: true });
    if (!gl) throw new Error("WebGL2 unavailable");
    this.gl = gl;
    this.init();
  }

  private init() {
    const gl = this.gl;
    this.progs = {
      solid: mkProg(gl, VS_STD, FS_SOLID, ["aPos", "aNrm", "aGrp"]),
      field: mkProg(gl, VS_STD, FS_FIELD, ["aPos", "aNrm", "aGrp"]),
      line: mkProg(gl, VS_LINE, FS_LINE, ["aPos"]),
      vcol: mkProg(gl, VS_VCOL, FS_VCOL, ["aPos", "aCol"]),
      mesh: mkProg(gl, VS_MESH, FS_MESH, ["aPos", "aCol", "aVal"]),
      slice: mkProg(gl, VS_SLICE, FS_SLICE, ["aPuv"]),
      scr: mkProg(gl, VS_SCR, FS_SCR, ["aPuv"]),
      triad: mkProg(gl, VS_TRIAD, FS_TRIAD, ["aP"]),
    };
    this.vaoTriad = gl.createVertexArray()!;
    gl.bindVertexArray(this.vaoTriad);
    const tb = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, tb);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(12), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    this.lut = gl.createTexture();
    this.vaoScr = this.mkQuadVAO();
    this.vaoSlice = this.mkSliceVAO();
    this.vaoGizmo = this.mkGizmoVAO();
  }

  private mkBuf(data: Float32Array | Uint32Array, target?: number): WebGLBuffer {
    const gl = this.gl;
    const b = gl.createBuffer()!;
    gl.bindBuffer(target ?? gl.ARRAY_BUFFER, b);
    gl.bufferData(target ?? gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
    return b;
  }
  private mkQuadVAO(): WebGLVertexArrayObject {
    const gl = this.gl;
    const v = gl.createVertexArray()!;
    gl.bindVertexArray(v);
    this.mkBuf(new Float32Array([-1, -1, 3, -1, -1, 3]));
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    return v;
  }
  private mkSliceVAO(): WebGLVertexArrayObject {
    const gl = this.gl;
    const v = gl.createVertexArray()!;
    gl.bindVertexArray(v);
    this.mkBuf(new Float32Array([-1, -1, 1, -1, 1, 1, -1, 1]));
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    return v;
  }
  private mkGizmoVAO(): WebGLVertexArrayObject {
    const gl = this.gl;
    const v = gl.createVertexArray()!;
    gl.bindVertexArray(v);
    const b = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, b);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(36 * 3), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    return v;
  }

  private mkFBO(w: number, h: number) {
    const gl = this.gl;
    if (this.fbo) {
      gl.deleteFramebuffer(this.fbo);
      gl.deleteTexture(this.texAlb);
      gl.deleteTexture(this.texNrm);
      gl.deleteTexture(this.texId);
      gl.deleteRenderbuffer(this.dep);
    }
    const tex = () => {
      const t = gl.createTexture()!;
      gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      return t;
    };
    this.texAlb = tex(); this.texNrm = tex(); this.texId = tex();
    this.dep = gl.createRenderbuffer()!;
    gl.bindRenderbuffer(gl.RENDERBUFFER, this.dep);
    gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, w, h);
    const fb = gl.createFramebuffer()!;
    gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.texAlb, 0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, this.texNrm, 0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT2, gl.TEXTURE_2D, this.texId, 0);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, this.dep);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1, gl.COLOR_ATTACHMENT2]);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this.fbo = fb;
  }

  private uploadLUT() {
    const gl = this.gl;
    this.lutData = buildLUT(this.lutKey.split("|")[0] as never, parseInt(this.lutKey.split("|")[1]) || 0);
    gl.bindTexture(gl.TEXTURE_2D, this.lut);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 256, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, this.lutData);
  }

  private rebuild() {
    const gl = this.gl;
    const d = geoCache.domain;
    const m = geoCache.mesh;
    if (!d || !m) return;
    if (this.vaoSolid) gl.deleteVertexArray(this.vaoSolid);
    const vao = gl.createVertexArray()!;
    gl.bindVertexArray(vao);
    const pos = new Float32Array(d.verts.length);
    for (let i = 0; i < d.verts.length; i++) pos[i] = d.verts[i]; // f64 → f32 (prototype precision)
    const pb = this.mkBuf(pos);
    gl.bindBuffer(gl.ARRAY_BUFFER, pb);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    const nb = this.mkBuf(new Float32Array(d.normals));
    gl.bindBuffer(gl.ARRAY_BUFFER, nb);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
    const grp = new Float32Array(d.faceGroup.length);
    for (let i = 0; i < d.faceGroup.length; i++) grp[i] = d.faceGroup[i];
    const gb = this.mkBuf(grp);
    gl.bindBuffer(gl.ARRAY_BUFFER, gb);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 1, gl.FLOAT, false, 0, 0);
    const ib = this.mkBuf(d.tris, gl.ELEMENT_ARRAY_BUFFER);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
    gl.bindVertexArray(null);
    this.vaoSolid = vao;
    this.solidCount = d.tris.length;

    // wireframe edges from triangles
    const wv: number[] = [];
    for (let t = 0; t < d.tris.length; t += 3) {
      const [a, b, c] = [d.tris[t], d.tris[t + 1], d.tris[t + 2]];
      for (const [i0, i1] of [[a, b], [b, c], [c, a]] as const) {
        wv.push(
          pos[i0 * 3], pos[i0 * 3 + 1], pos[i0 * 3 + 2],
          pos[i1 * 3], pos[i1 * 3 + 1], pos[i1 * 3 + 2]
        );
      }
    }
    if (this.vaoWire) gl.deleteVertexArray(this.vaoWire);
    const vw = gl.createVertexArray()!;
    gl.bindVertexArray(vw);
    this.mkBuf(new Float32Array(wv));
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    this.vaoWire = vw;
    this.wireCount = wv.length / 3;

    const mkLineVao = (arr: Float32Array): [WebGLVertexArrayObject, number] => {
      const v = gl.createVertexArray()!;
      gl.bindVertexArray(v);
      this.mkBuf(arr);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
      gl.bindVertexArray(null);
      return [v, arr.length / 3];
    };
    if (this.vaoOpen) gl.deleteVertexArray(this.vaoOpen);
    if (this.vaoNon) gl.deleteVertexArray(this.vaoNon);
    [this.vaoOpen, this.openCount] = mkLineVao(d.openEdges);
    [this.vaoNon, this.nonCount] = mkLineVao(d.nonManifold);

    // mesh VAOs (one per metric)
    for (const old of this.vaoMesh) if (old) gl.deleteVertexArray(old);
    this.vaoMesh = METRICS.map((mt) => {
      const v = gl.createVertexArray()!;
      gl.bindVertexArray(v);
      this.mkBuf(m.triPos);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
      this.mkBuf(m.triCol[mt]);
      gl.enableVertexAttribArray(1);
      gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
      this.mkBuf(m.triVal[mt]);
      gl.enableVertexAttribArray(2);
      gl.vertexAttribPointer(2, 1, gl.FLOAT, false, 0, 0);
      gl.bindVertexArray(null);
      return v;
    });
    this.meshCount = m.nCells * 36;

    this.streamKey = ""; // force streamline rebuild
  }

  private rebuildStreams() {
    const gl = this.gl;
    const s = useStore.getState();
    const p = geoCache.params;
    if (!p) return;
    const dev = devFactor(s.activeT);
    const pos: number[] = [];
    const col: number[] = [];
    const seeds = seedStreamlines(p);
    for (const seed of seeds) {
      const pts = integrateStreamline(seed, dev, p, 88, 0.006);
      for (let i = 1; i < pts.length; i++) {
        const a = pts[i - 1], b = pts[i];
        pos.push(a[0], a[1], a[2], b[0], b[1], b[2]);
        const u = Math.hypot(b[0], b[1], b[2]);
        const t = Math.min(1, Math.max(0, u / 11.5));
        const li = Math.min(255, Math.round(t * 255)) * 4;
        const br = 0.4 + 0.6 * t;
        col.push(
          (this.lutData[li] / 255) * br,
          (this.lutData[li + 1] / 255) * br,
          (this.lutData[li + 2] / 255) * br
        );
      }
    }
    if (this.vaoStream) gl.deleteVertexArray(this.vaoStream);
    const v = gl.createVertexArray()!;
    gl.bindVertexArray(v);
    const pb = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, pb);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(pos), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    const cb = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, cb);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(col), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    this.vaoStream = v;
    this.streamCount = pos.length / 6;
  }

  // ── input ────────────────────────────────────────────────────────────────
  onDown(x: number, y: number, btn: number) {
    const s = useStore.getState();
    this.dragging = true;
    this.dragBtn = btn;
    this.lastX = this.downX = x;
    this.lastY = this.downY = y;
    if (btn === 0 && s.view.sliceOn && s.view.mode === "field" && s.ws !== 2) {
      const [hx, hy] = this.handleScreen;
      if (Math.hypot(x - hx, y - hy) < 18) this.dragSlice = true;
    }
  }
  onMove(x: number, y: number) {
    this.mouseX = x; this.mouseY = y;
    this.mouseMoved = true;
    if (!this.dragging) return;
    const dx = x - this.lastX;
    const dy = y - this.lastY;
    this.lastX = x; this.lastY = y;
    const s = useStore.getState();
    if (this.dragSlice) {
      const axis = s.view.sliceAxis;
      const d = axis === "y" ? -dy : axis === "x" ? dx : dx;
      s.setView({ sliceT: Math.min(0.97, Math.max(0.03, s.view.sliceT + d * 0.0012)) });
      return;
    }
    if (this.dragBtn === 0 && !this.dragSlice) {
      this.yaw -= dx * 0.0062;
      this.pitch = Math.min(1.45, Math.max(-1.45, this.pitch + dy * 0.0055));
    } else {
      // pan (right / middle / ctrl)
      const v = this.viewM;
      const k = this.dist * 0.0011;
      const rx = [v[0], v[4], v[8]], uy = [v[1], v[5], v[9]];
      this.target[0] += (-rx[0] * dx + uy[0] * dy) * k;
      this.target[1] += (-rx[1] * dx + uy[1] * dy) * k;
      this.target[2] += (-rx[2] * dx + uy[2] * dy) * k;
    }
  }
  onUp(x: number, y: number, btn: number) {
    const wasDrag = Math.hypot(x - this.downX, y - this.downY) > 4;
    const dragSlice = this.dragSlice;
    this.dragging = false;
    this.dragSlice = false;
    this.dragBtn = -1;
    if (btn === 0 && !wasDrag && !dragSlice) this.pick(x, y);
  }
  onWheel(dy: number) {
    this.dist = Math.min(55, Math.max(2.5, this.dist * Math.exp(dy * 0.0011)));
  }

  private pick(x: number, y: number) {
    const gl = this.gl;
    const s = useStore.getState();
    if (s.ws === 2 || !this.fbo) return;
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
    const buf = new Uint8Array(4);
    gl.readPixels(Math.floor(x), Math.floor(this.H - y), 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, buf);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const gid = Math.round(buf[0] * 255);
    const overlay = buf[2] > 127;
    s.setSelPatch(!overlay && gid >= 0 && gid <= 7 ? gid : null);
  }

  exportPNG() {
    const a = document.createElement("a");
    a.download = `hydrocore_ws${useStore.getState().ws}_${Date.now()}.png`;
    a.href = this.canvas.toDataURL("image/png");
    a.click();
  }

  // ── main loop ────────────────────────────────────────────────────────────
  start() {
    const loop = (t: number) => {
      this.raf = requestAnimationFrame(loop);
      try {
        this.frame(t);
      } catch (e) {
        console.error("frame error", e);
      }
    };
    this.raf = requestAnimationFrame(loop);
  }
  destroy() {
    cancelAnimationFrame(this.raf);
  }

  private frame(t: number) {
    const gl = this.gl;
    const dt = t - this.lastT;
    this.lastT = t;
    if (dt > 0) this.fpsEma = this.fpsEma * 0.92 + (1000 / dt) * 0.08;
    this.stats.fps = Math.round(this.fpsEma);
    const canvas = this.canvas;
    const W = canvas.width, H = canvas.height;
    if (W < 4 || H < 4) return;
    if (W !== this.W || H !== this.H) {
      this.W = W; this.H = H;
      this.mkFBO(W, H);
    }
    const s = useStore.getState();
    const dom = geoCache.domain;
    const dp = geoCache.params;
    gl.clearColor(BG[0], BG[1], BG[2], 1);
    if (!dom || !dp) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, W, H);
      gl.clear(gl.COLOR_BUFFER_BIT);
      return;
    }
    if (s.geoVersion !== this.builtVer) {
      this.builtVer = s.geoVersion;
      this.rebuild();
    }
    const lutKey = `${s.params.cmap}|${s.params.banding ? s.params.bands : 0}`;
    if (lutKey !== this.lutKey) {
      this.lutKey = lutKey;
      this.uploadLUT();
    }
    const stKey = `${s.activeT}|${s.params.cmap}|${s.geoVersion}|${s.view.streamlines}`;
    if (stKey !== this.streamKey) {
      this.streamKey = stKey;
      this.rebuildStreams();
    }

    // camera (f64 center → f32 relative translation)
    const cx = dp.L * 0.5, cy = dp.H * 0.45, cz = 0;
    const cp = Math.cos(this.pitch);
    const eye = [
      cx + this.dist * Math.sin(this.yaw) * cp,
      cy + this.dist * Math.sin(this.pitch),
      cz + this.dist * Math.cos(this.yaw) * cp,
    ];
    const P = perspRevZ(46 * (Math.PI / 180), W / H, 0.12);
    const V = lookAt(eye, this.target, [0, 1, 0]);
    this.viewM = V;
    const M = mul(P, V);
    const uTr = [cx, cy, cz];

    let draws = 0, tris = 0;
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.GEQUAL);
    gl.disable(gl.CULL_FACE);
    gl.disable(gl.BLEND);

    const quadMode = s.ws === 2;
    const timeSec = t / 1000;

    const loc = (pr: Prog, name: string): WebGLUniformLocation | null => {
      if (!pr.uni.has(name)) pr.uni.set(name, gl.getUniformLocation(pr.p, name));
      return pr.uni.get(name) ?? null;
    };
    const setCommon = (pr: Prog) => {
      gl.useProgram(pr.p);
      gl.uniformMatrix4fv(loc(pr, "uMVP"), false, M);
      gl.uniform3f(loc(pr, "uTr"), uTr[0], uTr[1], uTr[2]);
    };

    if (quadMode) {
      // ── 4-quadrant MeshErrorMap: direct to canvas (strictly flat)
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      const hw = W >> 1, hh = H >> 1;
      const pr = this.progs.mesh;
      gl.useProgram(pr.p);
      gl.uniformMatrix4fv(gl.getUniformLocation(pr.p, "uMVP"), false, M);
      gl.uniform3f(gl.getUniformLocation(pr.p, "uTr"), uTr[0], uTr[1], uTr[2]);
      gl.uniform1f(gl.getUniformLocation(pr.p, "uThr"), Number(s.params.threshold));
      gl.uniform1f(gl.getUniformLocation(pr.p, "uTime"), timeSec);
      gl.uniform1f(gl.getUniformLocation(pr.p, "uNanActive"), s.nNaN > 0 ? 1 : 0);
      gl.uniform1f(gl.getUniformLocation(pr.p, "uShowNaN"), s.params.show_nan ? 1 : 0);
      for (let i = 0; i < 4; i++) {
        const x = (i % 2) * hw, y = (i > 1 ? hh : 0);
        gl.viewport(x, y, hw, hh);
        gl.clearColor(BG[0], BG[1], BG[2], 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.bindVertexArray(this.vaoMesh[i]);
        gl.drawArrays(gl.TRIANGLES, 0, this.meshCount);
        draws++;
        tris += this.meshCount / 3;
      }
      gl.bindVertexArray(null);
      gl.viewport(0, 0, W, H);
    } else {
      // ── Phase 1: geometry → MRT
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
      gl.viewport(0, 0, W, H);
      gl.clearBufferfv(gl.COLOR, 0, [BG[0], BG[1], BG[2], 1]);
      gl.clearBufferfv(gl.COLOR, 1, [0, 0, 0, 1]);
      gl.clearBufferfv(gl.COLOR, 2, [1, 0, 0, 1]); // 255 = background (not pickable)
      gl.clearBufferfi(gl.DEPTH, 0, 0.0, 0); // reverse-Z: 0.0 = infinitely far

      if (s.view.mode === "cad") {
        const pr = this.progs.solid;
        setCommon(pr);
        gl.bindVertexArray(this.vaoSolid);
        gl.drawElements(gl.TRIANGLES, this.solidCount, gl.UNSIGNED_INT, 0);
        draws++; tris += this.solidCount / 3;
        if (s.view.wire) {
          const pl = this.progs.line;
          setCommon(pl);
          gl.uniform3f(gl.getUniformLocation(pl.p, "uColor"), 0.24, 0.26, 0.31);
          gl.bindVertexArray(this.vaoWire);
          gl.drawArrays(gl.LINES, 0, this.wireCount);
          draws++;
        }
      } else {
        // Mode 2: strictly unlit scientific
        const pr = this.progs.field;
        setCommon(pr);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.lut);
        gl.uniform1i(gl.getUniformLocation(pr.p, "uLUT"), 0);
        gl.uniform1i(gl.getUniformLocation(pr.p, "uLic"), s.view.lic ? 1 : 0);
        gl.uniform4f(gl.getUniformLocation(pr.p, "uDom"), dp.L, dp.H, dp.h1, dp.stepX);
        gl.uniform4f(gl.getUniformLocation(pr.p, "uCyl"), dp.cylX, dp.cylY, dp.cylR, 0);
        gl.uniform1f(gl.getUniformLocation(pr.p, "uDev"), devFactor(s.activeT));
        gl.bindVertexArray(this.vaoSolid);
        gl.drawElements(gl.TRIANGLES, this.solidCount, gl.UNSIGNED_INT, 0);
        draws++; tris += this.solidCount / 3;

        if (s.view.sliceOn) {
          const ps = this.progs.slice;
          setCommon(ps);
          gl.uniform1i(gl.getUniformLocation(ps.p, "uLUT"), 0);
          gl.uniform4f(gl.getUniformLocation(ps.p, "uDom"), dp.L, dp.H, dp.h1, dp.stepX);
          gl.uniform4f(gl.getUniformLocation(ps.p, "uCyl"), dp.cylX, dp.cylY, dp.cylR, 0);
          gl.uniform1f(gl.getUniformLocation(ps.p, "uDev"), devFactor(s.activeT));
          const fd = FIELD_DEFS.find((f) => f.id === s.params.field_id) ?? FIELD_DEFS[0];
          const fi = FIELD_DEFS.indexOf(fd);
          gl.uniform1i(gl.getUniformLocation(ps.p, "uField"), fi);
          gl.uniform1f(gl.getUniformLocation(ps.p, "uMin"), fd.range[0]);
          gl.uniform1f(gl.getUniformLocation(ps.p, "uMax"), fd.range[1]);
          gl.uniform1f(gl.getUniformLocation(ps.p, "uBands"), s.params.banding ? Number(s.params.bands) : 0);
          const tv = s.view.sliceT;
          let O: number[], U: number[], Vv: number[];
          if (s.view.sliceAxis === "z") {
            O = [dp.L / 2, dp.H / 2, -dp.hw + tv * 2 * dp.hw];
            U = [dp.L / 2, 0, 0]; Vv = [0, dp.H / 2, 0];
          } else if (s.view.sliceAxis === "x") {
            O = [tv * dp.L, dp.H / 2, 0];
            U = [0, dp.H / 2, 0]; Vv = [0, 0, dp.hw];
          } else {
            O = [dp.L / 2, tv * dp.H, 0];
            U = [dp.L / 2, 0, 0]; Vv = [0, 0, dp.hw];
          }
          gl.uniform3f(gl.getUniformLocation(ps.p, "uO"), O[0], O[1], O[2]);
          gl.uniform3f(gl.getUniformLocation(ps.p, "uU"), U[0], U[1], U[2]);
          gl.uniform3f(gl.getUniformLocation(ps.p, "uV"), Vv[0], Vv[1], Vv[2]);
          // gizmo frame + handle
          const corners = [
            [O[0] - U[0] - Vv[0], O[1] - U[1] - Vv[1], O[2] - U[2] - Vv[2]],
            [O[0] + U[0] - Vv[0], O[1] + U[1] - Vv[1], O[2] + U[2] - Vv[2]],
            [O[0] + U[0] + Vv[0], O[1] + U[1] + Vv[1], O[2] + U[2] + Vv[2]],
            [O[0] - U[0] + Vv[0], O[1] - U[1] + Vv[1], O[2] - U[2] + Vv[2]],
          ];
          const g: number[] = [];
          for (let i = 0; i < 4; i++) {
            const a = corners[i], b = corners[(i + 1) % 4];
            g.push(a[0], a[1], a[2], b[0], b[1], b[2]);
          }
          const nn = s.view.sliceAxis === "z" ? [0, 0, 1] : s.view.sliceAxis === "x" ? [1, 0, 0] : [0, 1, 0];
          const hx = O[0], hy = O[1], hz = O[2], L1 = 0.28;
          g.push(
            hx - nn[0] * L1, hy - nn[1] * L1, hz - nn[2] * L1, hx + nn[0] * L1, hy + nn[1] * L1, hz + nn[2] * L1,
            hx - U[0] * 0.12, hy - U[1] * 0.12, hz - U[2] * 0.12, hx + U[0] * 0.12, hy + U[1] * 0.12, hz + U[2] * 0.12
          );
          gl.bindVertexArray(this.vaoSlice);
          gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
          draws++; tris += 2;
          const pg = this.progs.line;
          setCommon(pg);
          gl.uniform3f(gl.getUniformLocation(pg.p, "uColor"), LIME[0], LIME[1], LIME[2]);
          gl.bindVertexArray(this.vaoGizmo);
          const gb = gl.getParameter(gl.ARRAY_BUFFER_BINDING);
          gl.bindBuffer(gl.ARRAY_BUFFER, gb);
          gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(g), gl.DYNAMIC_DRAW);
          gl.drawArrays(gl.LINES, 0, g.length / 3);
          draws++;
          const [sx, sy] = this.screenPos([hx + nn[0] * L1, hy + nn[1] * L1, hz + nn[2] * L1], M, W, H);
          this.handleScreen = [sx, sy];
        }

        if (s.view.streamlines && this.vaoStream) {
          const pv = this.progs.vcol;
          setCommon(pv);
          gl.enable(gl.BLEND);
          gl.blendFunc(gl.ONE, gl.ONE);
          gl.depthMask(false);
          gl.bindVertexArray(this.vaoStream);
          gl.drawArrays(gl.LINES, 0, this.streamCount);
          draws++;
          gl.depthMask(true);
          gl.disable(gl.BLEND);
        }
      }

      // defect edge overlays (both modes)
      const pl = this.progs.line;
      setCommon(pl);
      if (this.openCount > 0) {
        gl.uniform3f(gl.getUniformLocation(pl.p, "uColor"), RED[0], RED[1], RED[2]);
        gl.bindVertexArray(this.vaoOpen);
        gl.drawArrays(gl.LINES, 0, this.openCount);
        draws++;
      }
      if (this.nonCount > 0) {
        gl.uniform3f(gl.getUniformLocation(pl.p, "uColor"), PINK[0], PINK[1], PINK[2]);
        gl.bindVertexArray(this.vaoNon);
        gl.drawArrays(gl.LINES, 0, this.nonCount);
        draws++;
      }

      // ── Phase 2: screen-space compositor
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, W, H);
      const ps = this.progs.scr;
      gl.useProgram(ps.p);
      gl.disable(gl.DEPTH_TEST);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.texAlb);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, this.texNrm);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, this.texId);
      gl.uniform1i(gl.getUniformLocation(ps.p, "uAlb"), 0);
      gl.uniform1i(gl.getUniformLocation(ps.p, "uNrm"), 1);
      gl.uniform1i(gl.getUniformLocation(ps.p, "uId"), 2);
      gl.uniform1i(gl.getUniformLocation(ps.p, "uMode"), s.view.mode === "cad" ? 0 : 1);
      gl.uniform1i(gl.getUniformLocation(ps.p, "uShade"), s.view.shade === "edl" ? 0 : s.view.shade === "cavity" ? 1 : 2);
      gl.uniform1i(gl.getUniformLocation(ps.p, "uSel"), s.selPatch ?? -1);
      gl.uniform1i(gl.getUniformLocation(ps.p, "uHover"), s.hoverPatch ?? -1);
      gl.uniform2f(gl.getUniformLocation(ps.p, "uRes"), W, H);
      gl.bindVertexArray(this.vaoScr);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      gl.enable(gl.DEPTH_TEST);
      draws++;

      // hover pick (throttled)
      if (this.mouseMoved && t - this.lastHover > 110 && !this.dragging && this.mouseX >= 0) {
        this.lastHover = t;
        this.mouseMoved = false;
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
        const buf = new Uint8Array(4);
        gl.readPixels(Math.floor(this.mouseX), Math.floor(H - this.mouseY), 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, buf);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        const gid = buf[2] > 127 ? -1 : Math.round(buf[0] * 255);
        if (gid !== s.hoverPatch) s.setHoverPatch(gid >= 0 && gid <= 7 ? gid : null);
      }

      this.drawTriad(W, H);
    }

    this.stats.draws = draws;
    this.stats.tris = Math.round(tris);
  }

  private screenPos(p: number[], M: Mat4, W: number, H: number): [number, number] {
    const x = p[0] - geoCache.params!.L * 0.5;
    const y = p[1] - geoCache.params!.H * 0.45;
    const z = p[2];
    const cw = M[3] * x + M[7] * y + M[11] * z + M[15];
    const cwx = M[0] * x + M[4] * y + M[8] * z + M[12];
    const cwz = M[1] * x + M[5] * y + M[9] * z + M[13];
    if (cw >= 0) return [-100, -100];
    return [((cwx / cw) * 0.5 + 0.5) * W, (1 - (cwz / cw) * 0.5 - 0.5) * H];
  }

  private drawTriad(W: number, H: number) {
    const gl = this.gl;
    const v = this.viewM;
    const ax = (d: [number, number, number]): [number, number] => [
      d[0] * v[0] + d[1] * v[4] + d[2] * v[8],
      d[0] * v[1] + d[1] * v[5] + d[2] * v[9],
    ];
    const cx = W - 64, cy = H - 64, r = 36;
    const data = new Float32Array(12);
    const axes: [number, number, number][] = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
    for (let i = 0; i < 3; i++) {
      const [sx, sy] = ax(axes[i]);
      data[i * 4] = cx;
      data[i * 4 + 1] = cy;
      data[i * 4 + 2] = cx + sx * r;
      data[i * 4 + 3] = cy + sy * r;
    }
    const pr = this.progs.triad;
    gl.useProgram(pr.p);
    gl.disable(gl.DEPTH_TEST);
    gl.uniform2f(gl.getUniformLocation(pr.p, "uRes"), W, H);
    gl.bindVertexArray(this.vaoTriad);
    const buf = gl.getParameter(gl.ARRAY_BUFFER_BINDING) as WebGLBuffer;
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
    const cols: [number, number, number][] = [RED, LIME, [0, 0.29, 0.992]];
    for (let i = 0; i < 3; i++) {
      gl.uniform3f(gl.getUniformLocation(pr.p, "uCol"), cols[i][0], cols[i][1], cols[i][2]);
      gl.drawArrays(gl.LINES, i * 2, 2);
    }
    gl.enable(gl.DEPTH_TEST);
    gl.bindVertexArray(null);
  }
}
