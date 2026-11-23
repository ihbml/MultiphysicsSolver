export type ViewportMode = "mesh" | "field" | "quality";
export type FieldName = "velocity" | "pressure" | "temperature" | "wall";
export type QualityMetric = "skewness" | "nonOrthogonality" | "aspectRatio" | "cellVolume";
export type PaletteName = "Dark Ember" | "Fire" | "Cool-Warm" | "Traffic Light" | "Viridis";

export type ViewportOptions = {
  mode: ViewportMode;
  metric: QualityMetric;
  field: FieldName;
  palette: PaletteName;
  threshold: number;
  contours: boolean;
  isolate: boolean;
  wireframe: boolean;
  selectedPatch: string;
  sliceEnabled: boolean;
  sliceAxis: "x" | "y" | "z";
  slicePosition: number;
};

type Camera = { yaw: number; pitch: number; radius: number };
type Vec3 = [number, number, number];

const METRIC_INDEX: Record<QualityMetric, number> = {
  skewness: 0,
  nonOrthogonality: 1,
  aspectRatio: 2,
  cellVolume: 3,
};

const PATCH_GROUP: Record<string, number> = {
  "blade-wall": 1,
  "top-wall": 2,
  inlet: 3,
  outlet: 4,
};

const mainVertexShader = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aScalar;
layout(location = 3) in vec4 aMetrics;
layout(location = 4) in float aGroup;
layout(location = 5) in float aBad;
uniform mat4 uViewProjection;
uniform mat4 uView;
out vec3 vNormal;
out vec3 vViewPosition;
out float vScalar;
out vec3 vRelativePosition;
flat out vec4 vMetrics;
flat out float vGroup;
flat out float vBad;
void main() {
  vec4 viewPosition = uView * vec4(aPosition, 1.0);
  gl_Position = uViewProjection * vec4(aPosition, 1.0);
  vNormal = mat3(uView) * aNormal;
  vViewPosition = viewPosition.xyz;
  vScalar = aScalar;
  vRelativePosition = aPosition;
  vMetrics = aMetrics;
  vGroup = aGroup;
  vBad = aBad;
}`;

const mainFragmentShader = `#version 300 es
precision highp float;
in vec3 vNormal;
in vec3 vViewPosition;
in float vScalar;
in vec3 vRelativePosition;
flat in vec4 vMetrics;
flat in float vGroup;
flat in float vBad;
uniform int uMode;
uniform int uMetric;
uniform int uPalette;
uniform float uThreshold;
uniform float uSelectedGroup;
uniform float uIsolate;
uniform float uContours;
uniform float uSliceEnabled;
uniform vec4 uSlicePlane;
out vec4 outColor;
vec3 viridis(float t) {
  vec3 a = vec3(0.267, 0.005, 0.329);
  vec3 b = vec3(0.230, 0.322, 0.546);
  vec3 c = vec3(0.128, 0.567, 0.551);
  vec3 d = vec3(0.369, 0.789, 0.383);
  vec3 e = vec3(0.993, 0.906, 0.144);
  return t < 0.25 ? mix(a, b, t * 4.0) :
         t < 0.5 ? mix(b, c, (t - 0.25) * 4.0) :
         t < 0.75 ? mix(c, d, (t - 0.5) * 4.0) : mix(d, e, (t - 0.75) * 4.0);
}
vec3 palette(float t) {
  t = clamp(t, 0.0, 1.0);
  if (uPalette == 0) {
    vec3 a = vec3(0.05, 0.02, 0.06); vec3 b = vec3(0.36, 0.09, 0.10);
    vec3 c = vec3(0.82, 0.27, 0.08); vec3 d = vec3(1.0, 0.77, 0.39);
    return t < 0.36 ? mix(a, b, t / 0.36) : t < 0.72 ? mix(b, c, (t - 0.36) / 0.36) : mix(c, d, (t - 0.72) / 0.28);
  }
  if (uPalette == 1) {
    vec3 a = vec3(0.18, 0.015, 0.02); vec3 b = vec3(0.82, 0.07, 0.025);
    vec3 c = vec3(1.0, 0.39, 0.04); vec3 d = vec3(1.0, 0.91, 0.53);
    return t < 0.4 ? mix(a, b, t / 0.4) : t < 0.76 ? mix(b, c, (t - 0.4) / 0.36) : mix(c, d, (t - 0.76) / 0.24);
  }
  if (uPalette == 2) {
    vec3 a = vec3(0.23, 0.35, 0.69); vec3 b = vec3(0.82, 0.84, 0.82); vec3 c = vec3(0.72, 0.15, 0.16);
    return t < 0.5 ? mix(a, b, t * 2.0) : mix(b, c, (t - 0.5) * 2.0);
  }
  if (uPalette == 3) {
    vec3 a = vec3(0.08, 0.46, 0.23); vec3 b = vec3(0.94, 0.82, 0.08); vec3 c = vec3(0.78, 0.08, 0.12);
    return t < 0.5 ? mix(a, b, t * 2.0) : mix(b, c, (t - 0.5) * 2.0);
  }
  return viridis(t);
}
void main() {
  if (uSliceEnabled > 0.5) {
    float sliceDistance = dot(vRelativePosition, uSlicePlane.xyz) - uSlicePlane.w;
    if (sliceDistance < -0.0005) discard;
    if (abs(sliceDistance) < 0.001) {
      outColor = vec4(0.72, 0.96, 0.0, 1.0);
      return;
    }
  }
  if (uMode == 0) {
    vec3 n = normalize(vNormal);
    vec3 viewDirection = normalize(-vViewPosition);
    float matcap = 0.36 + 0.54 * max(dot(n, normalize(vec3(-0.35, 0.72, 0.8))), 0.0);
    float rim = pow(1.0 - max(dot(n, viewDirection), 0.0), 2.2) * 0.23;
    float cavity = 0.9 + 0.1 * max(dot(n, normalize(vec3(0.2, 0.4, 1.0))), 0.0);
    vec3 base = vec3(0.29, 0.38, 0.49) * matcap * cavity + rim * vec3(0.2, 0.55, 0.95);
    if (uSelectedGroup > 0.0 && abs(vGroup - uSelectedGroup) < 0.5) base = mix(base, vec3(0.72, 0.96, 0.02), 0.58);
    outColor = vec4(base, 1.0);
    return;
  }
  float value = uMode == 2 ? vMetrics[uMetric] : vScalar;
  if (uContours > 0.5) value = floor(value * 16.0 + 0.5) / 16.0;
  vec3 color = palette(value);
  if (uMode == 2 && uIsolate > 0.5 && value < uThreshold) color = mix(color, vec3(0.09, 0.10, 0.12), 0.82);
  if (uMode == 2 && vBad > 0.5) color = vec3(0.94, 0.07, 0.19);
  if (uSelectedGroup > 0.0 && abs(vGroup - uSelectedGroup) < 0.5) color = mix(color, vec3(0.72, 0.96, 0.02), 0.55);
  outColor = vec4(color, 1.0);
}`;

const lineVertexShader = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
layout(location = 2) in float aPhase;
uniform mat4 uViewProjection;
uniform float uTime;
uniform float uAnimate;
out vec3 vColor;
void main() {
  vec3 position = aPosition;
  position.y += uAnimate * 0.008 * sin(position.x * 2.8 + uTime * 1.4 + aPhase);
  gl_Position = uViewProjection * vec4(position, 1.0);
  vColor = aColor;
}`;

const lineFragmentShader = `#version 300 es
precision highp float;
in vec3 vColor;
out vec4 outColor;
void main() { outColor = vec4(vColor, 1.0); }`;

const ribbonVertexShader = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aStart;
layout(location = 1) in vec3 aEnd;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aPhase;
uniform mat4 uViewProjection;
uniform mat4 uView;
uniform float uTime;
uniform float uWidth;
uniform float uPulse;
out vec3 vColor;
out float vAlpha;
void main() {
  float along = (gl_VertexID == 1 || gl_VertexID == 3) ? 1.0 : 0.0;
  float sideSign = (gl_VertexID == 0 || gl_VertexID == 1) ? -1.0 : 1.0;
  vec3 center = mix(aStart, aEnd, along);
  float phase = uTime * 1.5 + aPhase + along * 4.0;
  center.y += 0.003 * sin(phase);
  vec3 tangent = normalize(aEnd - aStart);
  vec3 viewDirection = normalize(-(uView * vec4(center, 1.0)).xyz);
  vec3 side = normalize(cross(viewDirection, tangent));
  center += side * sideSign * uWidth * (1.0 + uPulse * sin(phase));
  gl_Position = uViewProjection * vec4(center, 1.0);
  vColor = aColor * (0.83 + 0.17 * sin(phase));
  vAlpha = 0.78 + 0.22 * sin(phase);
}`;

const ribbonFragmentShader = `#version 300 es
precision highp float;
in vec3 vColor;
in float vAlpha;
out vec4 outColor;
void main() { outColor = vec4(vColor, vAlpha); }`;

const pickVertexShader = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 4) in float aGroup;
uniform mat4 uViewProjection;
flat out uint vId;
void main() {
  gl_Position = uViewProjection * vec4(aPosition, 1.0);
  vId = uint(aGroup + 0.5);
}`;

const pickFragmentShader = `#version 300 es
precision highp float;
flat in uint vId;
out vec4 outColor;
void main() {
  outColor = vec4(float(vId & 255u) / 255.0, float((vId >> 8u) & 255u) / 255.0, 0.0, 1.0);
}`;

function createProgram(gl: WebGL2RenderingContext, vertexSource: string, fragmentSource: string) {
  const compile = (type: number, source: string) => {
    const shader = gl.createShader(type);
    if (!shader) throw new Error("WebGL shader allocation failed");
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      const message = gl.getShaderInfoLog(shader) || "unknown shader error";
      gl.deleteShader(shader);
      throw new Error(message);
    }
    return shader;
  };
  const vertex = compile(gl.VERTEX_SHADER, vertexSource);
  const fragment = compile(gl.FRAGMENT_SHADER, fragmentSource);
  const program = gl.createProgram();
  if (!program) throw new Error("WebGL program allocation failed");
  gl.attachShader(program, vertex);
  gl.attachShader(program, fragment);
  gl.linkProgram(program);
  gl.deleteShader(vertex);
  gl.deleteShader(fragment);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    const message = gl.getProgramInfoLog(program) || "unknown program link error";
    gl.deleteProgram(program);
    throw new Error(message);
  }
  return program;
}

function vec3Subtract(a: Vec3, b: Vec3): Vec3 {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

function vec3Cross(a: Vec3, b: Vec3): Vec3 {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}

function vec3Normalize(v: Vec3): Vec3 {
  const length = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / length, v[1] / length, v[2] / length];
}

function vec3Dot(a: Vec3, b: Vec3) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

function lookAt(eye: Vec3, target: Vec3, up: Vec3) {
  const z = vec3Normalize(vec3Subtract(eye, target));
  const x = vec3Normalize(vec3Cross(up, z));
  const y = vec3Cross(z, x);
  return new Float32Array([
    x[0], y[0], z[0], 0,
    x[1], y[1], z[1], 0,
    x[2], y[2], z[2], 0,
    -vec3Dot(x, eye), -vec3Dot(y, eye), -vec3Dot(z, eye), 1,
  ]);
}

function multiplyMat4(a: Float32Array, b: Float32Array) {
  const out = new Float32Array(16);
  for (let column = 0; column < 4; column++) {
    for (let row = 0; row < 4; row++) {
      out[column * 4 + row] = a[row] * b[column * 4] + a[4 + row] * b[column * 4 + 1] + a[8 + row] * b[column * 4 + 2] + a[12 + row] * b[column * 4 + 3];
    }
  }
  return out;
}

function reverseInfiniteProjection(fov: number, aspect: number, near: number) {
  const f = 1 / Math.tan(fov / 2);
  return new Float32Array([
    f / aspect, 0, 0, 0,
    0, f, 0, 0,
    0, 0, 1, -1,
    0, 0, 2 * near, 0,
  ]);
}

function foilThickness(t: number) {
  const t2 = t * t;
  return 5 * 0.18 * (0.2969 * Math.sqrt(Math.max(t, 0)) - 0.126 * t - 0.3516 * t2 + 0.2843 * t2 * t - 0.1015 * t2 * t2);
}

function fieldValue(t: number, z: number, field: FieldName) {
  const peak = Math.exp(-Math.pow((t - 0.31) / 0.17, 2));
  const spanRipple = 0.07 * Math.cos(z * 12 + t * 5);
  if (field === "pressure") return Math.max(0, Math.min(1, 0.74 - 0.48 * peak + spanRipple));
  if (field === "temperature") return Math.max(0, Math.min(1, 0.24 + 0.38 * t + spanRipple));
  if (field === "wall") return Math.max(0, Math.min(1, 0.16 + 0.73 * Math.exp(-Math.pow((t - 0.68) / 0.2, 2)) + spanRipple));
  return Math.max(0, Math.min(1, 0.2 + 0.64 * peak + spanRipple));
}

function metricValues(t: number, span: number): [number, number, number, number] {
  const ripple = Math.abs(Math.sin(t * 14.7 + span * 21.3));
  return [
    0.08 + ripple * 0.76,
    0.12 + Math.abs(Math.cos(t * 6.1 + span * 9.4)) * 0.68,
    0.11 + Math.abs(Math.sin(t * 9.2 + span * 4.1)) * 0.82,
    0.16 + Math.abs(Math.cos(t * 11.8 - span * 6.7)) * 0.61,
  ];
}

type SurfaceBuffers = {
  vao: WebGLVertexArrayObject;
  buffer: WebGLBuffer;
  indexBuffer: WebGLBuffer;
  indexCount: number;
};

type LineBuffers = {
  vao: WebGLVertexArrayObject;
  buffer: WebGLBuffer;
  ribbonVao: WebGLVertexArrayObject;
  ribbonBuffer: WebGLBuffer;
  count: number;
  flowStart: number;
  ribbonCount: number;
};

function makeSurface(gl: WebGL2RenderingContext, field: FieldName): SurfaceBuffers {
  const chordSegments = 72;
  const spanSegments = 12;
  const vertices: number[] = [];
  const indices: number[] = [];
  for (const side of [1, -1]) {
    for (let i = 0; i < chordSegments; i++) {
      const t0 = i / chordSegments;
      const t1 = (i + 1) / chordSegments;
      for (let j = 0; j < spanSegments; j++) {
        const s0 = j / spanSegments;
        const s1 = (j + 1) / spanSegments;
        const cellT = (t0 + t1) * 0.5;
        const cellS = (s0 + s1) * 0.5;
        const metrics = metricValues(cellT, cellS);
        const bad = (i === 65 && j === 2) || (i === 67 && j === 9) ? 1 : 0;
        const first = vertices.length / 13;
        const corners: [number, number][] = [[t0, s0], [t1, s0], [t1, s1], [t0, s1]];
        for (const [t, span] of corners) {
          const x = -0.54 + t * 1.08;
          const y = side * foilThickness(t) + 0.015 * Math.sin(t * Math.PI);
          const z = -0.24 + span * 0.48;
          const derivative = (foilThickness(Math.min(0.999, t + 0.002)) - foilThickness(Math.max(0.001, t - 0.002))) / 0.004;
          const normal = vec3Normalize([-side * derivative, side, 0]);
          vertices.push(x, y, z, normal[0], normal[1], normal[2], fieldValue(t, z, field), metrics[0], metrics[1], metrics[2], metrics[3], 1, bad);
        }
        if (side > 0) indices.push(first, first + 3, first + 2, first, first + 2, first + 1);
        else indices.push(first, first + 1, first + 2, first, first + 2, first + 3);
      }
    }
  }
  const data = new Float32Array(vertices);
  const indexData = new Uint32Array(indices);
  const vao = gl.createVertexArray();
  const buffer = gl.createBuffer();
  const indexBuffer = gl.createBuffer();
  if (!vao || !buffer || !indexBuffer) throw new Error("Unable to allocate viewport buffers");
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
  gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indexData, gl.STATIC_DRAW);
  const stride = 13 * 4;
  gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, stride, 0);
  gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, stride, 3 * 4);
  gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 1, gl.FLOAT, false, stride, 6 * 4);
  gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 4, gl.FLOAT, false, stride, 7 * 4);
  gl.enableVertexAttribArray(4); gl.vertexAttribPointer(4, 1, gl.FLOAT, false, stride, 11 * 4);
  gl.enableVertexAttribArray(5); gl.vertexAttribPointer(5, 1, gl.FLOAT, false, stride, 12 * 4);
  gl.bindVertexArray(null);
  return { vao, buffer, indexBuffer, indexCount: indexData.length };
}

function makeLineData() {
  const lines: number[] = [];
  const ribbons: number[] = [];
  const add = (a: Vec3, b: Vec3, color: Vec3, phase = 0) => {
    lines.push(a[0], a[1], a[2], color[0], color[1], color[2], phase);
    lines.push(b[0], b[1], b[2], color[0], color[1], color[2], phase);
  };
  const boxColor: Vec3 = [0.12, 0.17, 0.22];
  const brightBox: Vec3 = [0.19, 0.26, 0.32];
  const x0 = -1.14; const x1 = 1.14; const y0 = -0.47; const y1 = 0.47; const z0 = -0.38; const z1 = 0.38;
  const corners: Vec3[] = [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0], [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]];
  const edges = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
  for (const [a, b] of edges) add(corners[a], corners[b], brightBox);
  for (let i = 1; i < 8; i++) {
    const x = x0 + ((x1 - x0) * i) / 8;
    add([x, y0, z0], [x, y0, z1], boxColor);
    const z = z0 + ((z1 - z0) * i) / 8;
    add([x0, y0, z], [x1, y0, z], boxColor);
  }
  for (let k = 0; k <= 12; k++) {
    const z = -0.24 + (k / 12) * 0.48;
    for (let i = 0; i < 72; i += 2) {
      const t0 = i / 72; const t1 = (i + 2) / 72;
      for (const side of [1, -1]) {
        const y0s = side * foilThickness(t0) + 0.015 * Math.sin(t0 * Math.PI);
        const y1s = side * foilThickness(t1) + 0.015 * Math.sin(t1 * Math.PI);
        add([-0.54 + t0 * 1.08, y0s, z], [-0.54 + t1 * 1.08, y1s, z], [0.13, 0.20, 0.26]);
      }
    }
  }
  for (let i = 0; i <= 72; i += 6) {
    const t = i / 72;
    for (const side of [1, -1]) {
      const y = side * foilThickness(t) + 0.015 * Math.sin(t * Math.PI);
      add([-0.54 + t * 1.08, y, -0.24], [-0.54 + t * 1.08, y, 0.24], [0.18, 0.25, 0.32]);
    }
  }
  add([0.536, 0.016, -0.24], [0.536, 0.016, 0.24], [0.94, 0.08, 0.18]);
  add([-0.54, 0, 0.24], [0.54, 0, 0.24], [0.72, 0.96, 0.02]);
  const flowStart = lines.length / 7;
  const streamColors: Vec3[] = [[0.06, 0.47, 0.98], [0.08, 0.68, 0.97], [0.14, 0.81, 0.83], [0.47, 0.86, 0.38], [0.80, 0.90, 0.08], [0.99, 0.73, 0.04]];
  const seeds = [-0.40, -0.32, -0.24, -0.16, 0.16, 0.24, 0.32, 0.40];
  seeds.forEach((seed, lineIndex) => {
    const z = -0.26 + (lineIndex % 4) * 0.17;
    const sign = Math.sign(seed);
    for (let i = 0; i < 40; i++) {
      const xA = -1.12 + (2.24 * i) / 40; const xB = -1.12 + (2.24 * (i + 1)) / 40;
      const curve = (x: number) => sign * 0.11 * Math.exp(-Math.pow((x - 0.02) / 0.34, 2));
      const yA = seed + curve(xA); const yB = seed + curve(xB);
      const speed = Math.min(4.99, (lineIndex + i / 40) / 1.6);
      const colorIndex = Math.floor(speed); const mix = speed - colorIndex;
      const a = streamColors[colorIndex]; const b = streamColors[colorIndex + 1];
      const color: Vec3 = [a[0] * (1 - mix) + b[0] * mix, a[1] * (1 - mix) + b[1] * mix, a[2] * (1 - mix) + b[2] * mix];
      add([xA, yA, z], [xB, yB, z], color, lineIndex * 0.9);
      ribbons.push(xA, yA, z, xB, yB, z, color[0], color[1], color[2], lineIndex * 0.9);
    }
  });
  return { data: new Float32Array(lines), ribbons: new Float32Array(ribbons), count: lines.length / 7, flowStart, ribbonCount: ribbons.length / 10 };
}

function makeLineBuffers(gl: WebGL2RenderingContext): LineBuffers {
  const lineData = makeLineData();
  const vao = gl.createVertexArray(); const buffer = gl.createBuffer();
  const ribbonVao = gl.createVertexArray(); const ribbonBuffer = gl.createBuffer();
  if (!vao || !buffer || !ribbonVao || !ribbonBuffer) throw new Error("Unable to allocate line buffers");
  gl.bindVertexArray(vao); gl.bindBuffer(gl.ARRAY_BUFFER, buffer); gl.bufferData(gl.ARRAY_BUFFER, lineData.data, gl.STATIC_DRAW);
  const stride = 7 * 4;
  gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, stride, 0);
  gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, stride, 3 * 4);
  gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 1, gl.FLOAT, false, stride, 6 * 4);
  gl.bindVertexArray(null);
  gl.bindVertexArray(ribbonVao); gl.bindBuffer(gl.ARRAY_BUFFER, ribbonBuffer); gl.bufferData(gl.ARRAY_BUFFER, lineData.ribbons, gl.STATIC_DRAW);
  const ribbonStride = 10 * 4;
  gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, ribbonStride, 0); gl.vertexAttribDivisor(0, 1);
  gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, ribbonStride, 3 * 4); gl.vertexAttribDivisor(1, 1);
  gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 3, gl.FLOAT, false, ribbonStride, 6 * 4); gl.vertexAttribDivisor(2, 1);
  gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 1, gl.FLOAT, false, ribbonStride, 9 * 4); gl.vertexAttribDivisor(3, 1);
  gl.bindVertexArray(null);
  return { vao, buffer, ribbonVao, ribbonBuffer, count: lineData.count, flowStart: lineData.flowStart, ribbonCount: lineData.ribbonCount };
}

export class CfdViewportRenderer {
  private readonly gl: WebGL2RenderingContext;
  private readonly mainProgram: WebGLProgram;
  private readonly lineProgram: WebGLProgram;
  private readonly ribbonProgram: WebGLProgram;
  private readonly pickProgram: WebGLProgram;
  private surface: SurfaceBuffers;
  private readonly lines: LineBuffers;
  private readonly pickFramebuffer: WebGLFramebuffer;
  private readonly pickTexture: WebGLTexture;
  private readonly pickDepth: WebGLRenderbuffer;
  private readonly resizeObserver: ResizeObserver;
  private options: ViewportOptions;
  private camera: Camera = { yaw: 0.76, pitch: 0.32, radius: 3.35 };
  private view = new Float32Array(16);
  private viewProjection = new Float32Array(16);
  private frame = 0;
  private down: { x: number; y: number; moved: boolean } | null = null;
  private disposed = false;
  private width = 1;
  private height = 1;
  private startTime = performance.now();
  private readonly onPointerDown = (event: PointerEvent) => this.pointerDown(event);
  private readonly onPointerMove = (event: PointerEvent) => this.pointerMove(event);
  private readonly onPointerUp = (event: PointerEvent) => this.pointerUp(event);
  private readonly onWheel = (event: WheelEvent) => this.wheel(event);
  private readonly preventContextMenu = (event: Event) => event.preventDefault();

  constructor(private readonly canvas: HTMLCanvasElement, private readonly onPick: (patch: string) => void, options: ViewportOptions) {
    const gl = canvas.getContext("webgl2", { antialias: true, alpha: true, depth: true, preserveDrawingBuffer: true });
    if (!gl) throw new Error("WebGL 2 is unavailable in this browser");
    this.gl = gl;
    this.options = options;
    this.mainProgram = createProgram(gl, mainVertexShader, mainFragmentShader);
    this.lineProgram = createProgram(gl, lineVertexShader, lineFragmentShader);
    this.ribbonProgram = createProgram(gl, ribbonVertexShader, ribbonFragmentShader);
    this.pickProgram = createProgram(gl, pickVertexShader, pickFragmentShader);
    this.surface = makeSurface(gl, options.field);
    this.lines = makeLineBuffers(gl);
    const framebuffer = gl.createFramebuffer(); const texture = gl.createTexture(); const depth = gl.createRenderbuffer();
    if (!framebuffer || !texture || !depth) throw new Error("Unable to allocate ID buffer");
    this.pickFramebuffer = framebuffer; this.pickTexture = texture; this.pickDepth = depth;
    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(canvas);
    canvas.addEventListener("pointerdown", this.onPointerDown);
    canvas.addEventListener("pointermove", this.onPointerMove);
    canvas.addEventListener("pointerup", this.onPointerUp);
    canvas.addEventListener("wheel", this.onWheel, { passive: false });
    canvas.addEventListener("contextmenu", this.preventContextMenu);
    this.resize();
    this.frame = requestAnimationFrame(this.render);
  }

  update(options: ViewportOptions) {
    const fieldChanged = options.field !== this.options.field;
    this.options = options;
    if (fieldChanged) {
      const gl = this.gl;
      gl.deleteVertexArray(this.surface.vao); gl.deleteBuffer(this.surface.buffer); gl.deleteBuffer(this.surface.indexBuffer);
      this.surface = makeSurface(gl, options.field);
    }
  }

  resetCamera() { this.camera = { yaw: 0.76, pitch: 0.32, radius: 3.35 }; }

  dispose() {
    this.disposed = true; cancelAnimationFrame(this.frame); this.resizeObserver.disconnect();
    this.canvas.removeEventListener("pointerdown", this.onPointerDown);
    this.canvas.removeEventListener("pointermove", this.onPointerMove);
    this.canvas.removeEventListener("pointerup", this.onPointerUp);
    this.canvas.removeEventListener("wheel", this.onWheel);
    this.canvas.removeEventListener("contextmenu", this.preventContextMenu);
    const gl = this.gl;
    gl.deleteVertexArray(this.surface.vao); gl.deleteBuffer(this.surface.buffer); gl.deleteBuffer(this.surface.indexBuffer);
    gl.deleteVertexArray(this.lines.vao); gl.deleteBuffer(this.lines.buffer);
    gl.deleteVertexArray(this.lines.ribbonVao); gl.deleteBuffer(this.lines.ribbonBuffer);
    gl.deleteFramebuffer(this.pickFramebuffer); gl.deleteTexture(this.pickTexture); gl.deleteRenderbuffer(this.pickDepth);
    gl.deleteProgram(this.mainProgram); gl.deleteProgram(this.lineProgram); gl.deleteProgram(this.ribbonProgram); gl.deleteProgram(this.pickProgram);
  }

  private resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2); const bounds = this.canvas.getBoundingClientRect();
    this.width = Math.max(1, Math.floor(bounds.width * dpr)); this.height = Math.max(1, Math.floor(bounds.height * dpr));
    if (this.canvas.width !== this.width || this.canvas.height !== this.height) { this.canvas.width = this.width; this.canvas.height = this.height; }
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.pickTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, this.width, this.height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.bindRenderbuffer(gl.RENDERBUFFER, this.pickDepth); gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, this.width, this.height);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.pickFramebuffer);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.pickTexture, 0);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, this.pickDepth);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  private updateMatrices() {
    const { yaw, pitch, radius } = this.camera;
    const eye: Vec3 = [Math.sin(yaw) * radius * Math.cos(pitch), Math.sin(pitch) * radius, Math.cos(yaw) * radius * Math.cos(pitch)];
    this.view = lookAt(eye, [0, 0, 0], [0, 1, 0]);
    this.viewProjection = multiplyMat4(reverseInfiniteProjection(Math.PI / 4.2, this.width / this.height, 0.018), this.view);
  }

  private uniformMatrix(program: WebGLProgram, name: string, matrix: Float32Array) {
    const location = this.gl.getUniformLocation(program, name);
    if (location) this.gl.uniformMatrix4fv(location, false, matrix);
  }

  private render = (timestamp: number) => {
    if (this.disposed) return;
    const gl = this.gl; this.updateMatrices();
    gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.viewport(0, 0, this.width, this.height);
    gl.clearColor(0.035, 0.043, 0.055, 1); gl.clearDepth(0); gl.depthFunc(gl.GREATER); gl.enable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.useProgram(this.mainProgram);
    this.uniformMatrix(this.mainProgram, "uViewProjection", this.viewProjection); this.uniformMatrix(this.mainProgram, "uView", this.view);
    const mode = this.options.mode === "mesh" ? 0 : this.options.mode === "field" ? 1 : 2;
    const paletteIndex = ["Dark Ember", "Fire", "Cool-Warm", "Traffic Light", "Viridis"].indexOf(this.options.palette);
    gl.uniform1i(gl.getUniformLocation(this.mainProgram, "uMode"), mode);
    gl.uniform1i(gl.getUniformLocation(this.mainProgram, "uMetric"), METRIC_INDEX[this.options.metric]);
    gl.uniform1i(gl.getUniformLocation(this.mainProgram, "uPalette"), Math.max(0, paletteIndex));
    gl.uniform1f(gl.getUniformLocation(this.mainProgram, "uThreshold"), this.options.threshold);
    gl.uniform1f(gl.getUniformLocation(this.mainProgram, "uSelectedGroup"), PATCH_GROUP[this.options.selectedPatch] ?? 0);
    gl.uniform1f(gl.getUniformLocation(this.mainProgram, "uIsolate"), this.options.isolate ? 1 : 0);
    gl.uniform1f(gl.getUniformLocation(this.mainProgram, "uContours"), this.options.contours ? 1 : 0);
    gl.uniform1f(gl.getUniformLocation(this.mainProgram, "uSliceEnabled"), this.options.sliceEnabled ? 1 : 0);
    const sliceNormal: Vec3 = this.options.sliceAxis === "x" ? [1, 0, 0] : this.options.sliceAxis === "y" ? [0, 1, 0] : [0, 0, 1];
    gl.uniform4f(gl.getUniformLocation(this.mainProgram, "uSlicePlane"), sliceNormal[0], sliceNormal[1], sliceNormal[2], this.options.slicePosition);
    gl.bindVertexArray(this.surface.vao); gl.drawElements(gl.TRIANGLES, this.surface.indexCount, gl.UNSIGNED_INT, 0);
    gl.useProgram(this.lineProgram); this.uniformMatrix(this.lineProgram, "uViewProjection", this.viewProjection);
    gl.uniform1f(gl.getUniformLocation(this.lineProgram, "uTime"), (timestamp - this.startTime) / 1000);
    gl.uniform1f(gl.getUniformLocation(this.lineProgram, "uAnimate"), 0);
    gl.bindVertexArray(this.lines.vao); gl.depthFunc(gl.GEQUAL);
    if (this.options.wireframe) gl.drawArrays(gl.LINES, 0, this.lines.flowStart);
    if (this.options.mode === "field") {
      gl.useProgram(this.ribbonProgram);
      this.uniformMatrix(this.ribbonProgram, "uViewProjection", this.viewProjection);
      this.uniformMatrix(this.ribbonProgram, "uView", this.view);
      gl.uniform1f(gl.getUniformLocation(this.ribbonProgram, "uTime"), (timestamp - this.startTime) / 1000);
      gl.uniform1f(gl.getUniformLocation(this.ribbonProgram, "uWidth"), 0.006);
      gl.uniform1f(gl.getUniformLocation(this.ribbonProgram, "uPulse"), 0.08);
      gl.bindVertexArray(this.lines.ribbonVao);
      gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, this.lines.ribbonCount);
    }
    gl.bindVertexArray(null);
    this.frame = requestAnimationFrame(this.render);
  };

  private pointerDown(event: PointerEvent) {
    if (event.button !== 0) return;
    this.down = { x: event.clientX, y: event.clientY, moved: false };
    this.canvas.setPointerCapture(event.pointerId);
  }

  private pointerMove(event: PointerEvent) {
    if (!this.down) return;
    const dx = event.movementX; const dy = event.movementY;
    if (Math.abs(event.clientX - this.down.x) + Math.abs(event.clientY - this.down.y) > 4) this.down.moved = true;
    if (this.down.moved) {
      this.camera.yaw += dx * 0.008;
      this.camera.pitch = Math.max(-1.2, Math.min(1.2, this.camera.pitch + dy * 0.006));
    }
  }

  private pointerUp(event: PointerEvent) {
    if (!this.down) return;
    const didMove = this.down.moved; this.down = null;
    if (!didMove) this.pick(event.clientX, event.clientY);
  }

  private wheel(event: WheelEvent) {
    event.preventDefault(); this.camera.radius = Math.max(1.9, Math.min(6.5, this.camera.radius * Math.exp(event.deltaY * 0.001)));
  }

  private pick(clientX: number, clientY: number) {
    const gl = this.gl; const bounds = this.canvas.getBoundingClientRect();
    const dprX = this.width / Math.max(bounds.width, 1); const dprY = this.height / Math.max(bounds.height, 1);
    const x = Math.max(0, Math.min(this.width - 1, Math.floor((clientX - bounds.left) * dprX)));
    const y = Math.max(0, Math.min(this.height - 1, this.height - 1 - Math.floor((clientY - bounds.top) * dprY)));
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.pickFramebuffer); gl.viewport(0, 0, this.width, this.height);
    gl.clearColor(0, 0, 0, 0); gl.clearDepth(0); gl.depthFunc(gl.GREATER); gl.enable(gl.DEPTH_TEST); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.useProgram(this.pickProgram); this.uniformMatrix(this.pickProgram, "uViewProjection", this.viewProjection);
    gl.bindVertexArray(this.surface.vao); gl.drawElements(gl.TRIANGLES, this.surface.indexCount, gl.UNSIGNED_INT, 0);
    const pixel = new Uint8Array(4); gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
    gl.bindVertexArray(null); gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    if (pixel[0] + pixel[1] * 256 === 1) { this.onPick("blade-wall"); return; }
    const nx = (clientX - bounds.left) / bounds.width; const ny = (clientY - bounds.top) / bounds.height;
    if (nx < 0.13) this.onPick("inlet");
    else if (nx > 0.87) this.onPick("outlet");
    else if (ny < 0.16) this.onPick("top-wall");
    else if (ny > 0.86) this.onPick("bottom-wall");
    else this.onPick("blade-wall");
  }
}
