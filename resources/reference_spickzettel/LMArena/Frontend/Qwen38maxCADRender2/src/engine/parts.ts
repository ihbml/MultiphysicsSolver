import { type Vec3 } from "./math3d";
import {
  type MeshBuilder,
  axisXform,
  boxMesh,
  cylMesh,
  tr,
  tubeMesh,
  wedgeMesh,
} from "./geometry";

export interface Material {
  id: string;
  label: string;
  base: Vec3;
  spec: number;
  rough: number;
}

export const MATERIALS: Record<string, Material> = {
  cast: { id: "cast", label: "CastAl A356-T6", base: [124, 132, 145], spec: 0.22, rough: 0.78 },
  machined: { id: "machined", label: "Al 6082 milled", base: [172, 181, 192], spec: 0.45, rough: 0.42 },
  steel: { id: "steel", label: "1.4301 stainless", base: [150, 160, 174], spec: 0.6, rough: 0.3 },
  anodized: { id: "anodized", label: "Anodised amber", base: [228, 156, 52], spec: 0.5, rough: 0.35 },
  brass: { id: "brass", label: "CuZn39Pb3", base: [196, 163, 92], spec: 0.55, rough: 0.38 },
  rubber: { id: "rubber", label: "NBR 70 Shore", base: [58, 62, 70], spec: 0.08, rough: 0.95 },
};

export interface Solid {
  id: string;
  entity: string;
  label: string;
  group: string;
  material: string;
  mesh: MeshBuilder;
}

export interface GdtFeature {
  id: string;
  datum: string;
  frame: string;
  anchor: Vec3;
  elbow: Vec3;
  tail: Vec3;
}

export interface DimFeature {
  a: Vec3;
  b: Vec3;
  label: string;
}

export interface PartDef {
  id: string;
  name: string;
  code: string;
  standard: string;
  materialNote: string;
  mass: string;
  solids: Solid[];
  gdt: GdtFeature[];
  dims: DimFeature[];
  load: Vec3;
  support: Vec3;
  home: { yaw: number; pitch: number; dist: number; target: Vec3 };
  sectionRange: [number, number];
}

/** Tessellation quality factor driven by `geom.step_kernel`. */
export const LOD_LEVELS = [
  { id: 0, label: "Coarse", factor: 0.5, note: "0.18 mm chord" },
  { id: 1, label: "Standard", factor: 1, note: "0.06 mm chord" },
  { id: 2, label: "Fine", factor: 1.75, note: "0.02 mm chord" },
];

let L = 1;
const q = (n: number) => Math.max(6, Math.round(n * L));
const cs = (n: number) => n / L;

const H = Math.PI / 2;

function housing(): PartDef {
  const solids: Solid[] = [];
  const add = (s: Solid) => solids.push(s);

  add({
    id: "base_plate",
    entity: "part.body.base_plate",
    label: "Base Plate 54 × 34 × 5",
    group: "Body",
    material: "cast",
    mesh: boxMesh(5.4, 0.5, 3.4, tr([0, -0.9, 0]), cs(0.72)),
  });
  add({
    id: "flange_disc",
    entity: "part.body.flange_disc",
    label: "Mounting Flange Ø38.0",
    group: "Body",
    material: "machined",
    mesh: cylMesh(1.9, 0.28, q(44), tr([0, -0.65, 0]), true, false, cs(0.8)),
  });
  add({
    id: "boss_column",
    entity: "part.body.bearing_boss",
    label: "Bearing Boss Ø25.6",
    group: "Body",
    material: "cast",
    mesh: tubeMesh(1.28, 0.72, 1.5, q(36), tr([0, -0.37, 0]), true, false, cs(0.6)),
  });
  add({
    id: "bearing_sleeve",
    entity: "assy.bronze_sleeve",
    label: "Bronze Sleeve Ø19.0 h6",
    group: "Assembly",
    material: "brass",
    mesh: tubeMesh(0.95, 0.72, 0.32, q(32), tr([0, 1.13, 0]), true, false, cs(0.6)),
  });
  add({
    id: "top_collar",
    entity: "assy.retainer_collar",
    label: "Retainer Collar",
    group: "Assembly",
    material: "anodized",
    mesh: tubeMesh(1.5, 1.28, 0.16, q(32), tr([0, 1.13, 0]), true, true, cs(0.6)),
  });

  const pads: [number, number][] = [
    [-1.95, -1.05],
    [1.95, -1.05],
    [-1.95, 1.05],
    [1.95, 1.05],
  ];
  pads.forEach(([px, pz], i) => {
    add({
      id: `corner_bushing_${i}`,
      entity: `assy.bushing_0${i + 1}`,
      label: `Corner Bushing 0${i + 1}`,
      group: "Fasteners",
      material: "machined",
      mesh: tubeMesh(0.34, 0.18, 0.26, q(18), tr([px, -0.65, pz]), true, false, cs(0.6)),
    });
  });

  add({
    id: "grease_nipple",
    entity: "feat.grease_nipple",
    label: "Grease Nipple M6 × 1",
    group: "Features",
    material: "steel",
    mesh: cylMesh(0.11, 0.34, q(14), axisXform("x", [1.27, 0.55, 0]), true, false),
  });
  add({
    id: "oil_port",
    entity: "feat.oil_port_boss",
    label: "Oil Port Boss Ø6.0",
    group: "Features",
    material: "cast",
    mesh: cylMesh(0.3, 0.44, q(20), axisXform("z", [0, 0.25, 1.27]), true, false),
  });
  add({
    id: "data_plate",
    entity: "feat.data_plate",
    label: "Data Plate (laser etched)",
    group: "Features",
    material: "anodized",
    mesh: boxMesh(1.6, 0.5, 0.05, tr([0, -0.92, 1.725]), cs(0.5)),
  });

  return {
    id: "brg220",
    name: "Bearing Housing",
    code: "BRG-220",
    standard: "ISO 286 H7/j6 · ISO 1101",
    materialNote: "AlSi10Mg sand cast, CNC faced",
    mass: "2.84 kg",
    solids,
    gdt: [
      {
        id: "gdt_a",
        datum: "A",
        frame: "⌖ Ø0.05 Ⓜ A",
        anchor: [1.95, -0.65, 1.05],
        elbow: [2.9, 0.15, 1.75],
        tail: [3.9, 0.15, 1.75],
      },
      {
        id: "gdt_b",
        datum: "B",
        frame: "⟂ 0.02 A-B",
        anchor: [0, 1.29, 0.72],
        elbow: [1.35, 2.25, 1.1],
        tail: [2.6, 2.25, 1.1],
      },
      {
        id: "gdt_c",
        datum: "C",
        frame: "∥ 0.04 C",
        anchor: [-1.9, -0.5, 0.2],
        elbow: [-3.1, 0.55, -0.35],
        tail: [-4.1, 0.55, -0.35],
      },
    ],
    dims: [
      { a: [-2.7, -1.15, 2.2], b: [2.7, -1.15, 2.2], label: "54.00 ±0.1" },
      { a: [2.9, -1.15, -1.7], b: [2.9, 1.29, -1.7], label: "24.40" },
      { a: [-1.5, 1.35, 0], b: [1.5, 1.35, 0], label: "Ø30.0 H7" },
    ],
    load: [0, 1.29, 0],
    support: [0, -1.15, 0],
    home: { yaw: 0.66, pitch: 0.4, dist: 9.6, target: [0, 0.05, 0] },
    sectionRange: [-2.8, 2.8],
  };
}

function flange(): PartDef {
  const solids: Solid[] = [];
  const add = (s: Solid) => solids.push(s);

  add({
    id: "flange_disc",
    entity: "part.body.flange_disc",
    label: "Flange Disc Ø54.0 × 5",
    group: "Body",
    material: "machined",
    mesh: cylMesh(2.7, 0.5, q(48), tr([0, -1.0, 0]), true, true, cs(0.95)),
  });
  add({
    id: "raised_face",
    entity: "part.body.raised_face",
    label: "Raised Face RF Ø35.6",
    group: "Body",
    material: "steel",
    mesh: tubeMesh(1.78, 1.17, 0.18, q(44), tr([0, -0.5, 0]), true, false, cs(0.6)),
  });
  add({
    id: "neck",
    entity: "part.body.neck",
    label: "Neck Ø23.2 × 3",
    group: "Body",
    material: "cast",
    mesh: tubeMesh(1.16, 0.86, 1.9, q(40), tr([0, -0.32, 0]), false, false, cs(0.6)),
  });
  add({
    id: "weld_lip",
    entity: "part.body.weld_lip",
    label: "Weld Prep Lip 30°",
    group: "Body",
    material: "brass",
    mesh: tubeMesh(1.36, 0.86, 0.26, q(40), tr([0, 1.58, 0]), true, false, cs(0.6)),
  });

  for (let i = 0; i < 6; i++) {
    const a = (i / 6) * Math.PI * 2;
    add({
      id: `bolt_collar_${i}`,
      entity: `assy.bolt_hole_0${i + 1}`,
      label: `Bolt Hole 0${i + 1} Ø12 H13`,
      group: "Fasteners",
      material: "machined",
      mesh: tubeMesh(
        0.33,
        0.18,
        0.26,
        q(18),
        tr([Math.cos(a) * 2.18, -0.5, Math.sin(a) * 2.18]),
        true,
        false,
        cs(0.6),
      ),
    });
  }

  for (let i = 0; i < 3; i++) {
    const a = Math.PI / 6 + (i / 3) * Math.PI * 2;
    add({
      id: `neck_gusset_${i}`,
      entity: `part.body.gusset_0${i + 1}`,
      label: `Neck Gusset 0${i + 1} t3`,
      group: "Body",
      material: "cast",
      mesh: wedgeMesh([1.16, -0.5], [1.16, -0.08], [2.15, -0.5], 0.22, tr([0, 0, 0], [0, a, 0])),
    });
  }

  for (let i = 0; i < 3; i++) {
    const a = Math.PI / 2 + (i / 3) * Math.PI * 2;
    add({
      id: `support_foot_${i}`,
      entity: `assy.foot_0${i + 1}`,
      label: `Vibration Foot 0${i + 1}`,
      group: "Assembly",
      material: "rubber",
      mesh: cylMesh(0.45, 0.14, q(20), tr([Math.cos(a) * 2.0, -1.14, Math.sin(a) * 2.0]), false, true),
    });
  }

  add({
    id: "port_boss",
    entity: "feat.side_port",
    label: "Side Port G1/4",
    group: "Features",
    material: "cast",
    mesh: cylMesh(0.3, 0.66, q(22), axisXform("x", [1.13, 0.7, 0]), true, false),
  });

  return {
    id: "mfl6b",
    name: "Manifold Flange",
    code: "MFL-6B",
    standard: "EN 1092-1 PN16 · ASME B16.5",
    materialNote: "S355J2 turned, Zn-Ni plated",
    mass: "4.12 kg",
    solids,
    gdt: [
      {
        id: "gdt_a",
        datum: "A",
        frame: "⌖ Ø0.2 Ⓜ A-B",
        anchor: [2.18, -0.5, 0],
        elbow: [3.35, 0.35, 0.5],
        tail: [4.5, 0.35, 0.5],
      },
      {
        id: "gdt_b",
        datum: "B",
        frame: "◎ 0.05 B",
        anchor: [0, 1.84, 0.86],
        elbow: [1.3, 2.65, 1.3],
        tail: [2.5, 2.65, 1.3],
      },
      {
        id: "gdt_c",
        datum: "C",
        frame: "▱ 0.1 C",
        anchor: [-1.78, -0.5, 0.6],
        elbow: [-3.0, 0.4, 1.3],
        tail: [-4.2, 0.4, 1.3],
      },
    ],
    dims: [
      { a: [-2.7, -1.14, 3.0], b: [2.7, -1.14, 3.0], label: "Ø54.00" },
      { a: [3.1, -1.14, -2.2], b: [3.1, 1.84, -2.2], label: "29.80" },
      { a: [-2.18, -0.2, 0], b: [2.18, -0.2, 0], label: "43.60 BC" },
    ],
    load: [0, 1.84, 0],
    support: [0, -1.14, 0],
    home: { yaw: 0.95, pitch: 0.34, dist: 11.2, target: [0, 0.2, 0] },
    sectionRange: [-2.8, 2.8],
  };
}

function bracket(): PartDef {
  const solids: Solid[] = [];
  const add = (s: Solid) => solids.push(s);

  add({
    id: "foot_plate",
    entity: "part.body.foot_plate",
    label: "Foot Plate 42 × 28 × 4.5",
    group: "Body",
    material: "cast",
    mesh: boxMesh(4.2, 0.45, 2.8, tr([0, -1.225, 0]), cs(0.75)),
  });
  add({
    id: "web_plate",
    entity: "part.body.vertical_web",
    label: "Vertical Web t4.5",
    group: "Body",
    material: "cast",
    mesh: boxMesh(0.45, 3.0, 2.8, tr([-1.375, 0.5, 0]), cs(0.75)),
  });
  [-1.05, 1.05].forEach((z, i) => {
    add({
      id: `corner_gusset_${i}`,
      entity: `part.body.gusset_0${i + 1}`,
      label: `Corner Gusset 0${i + 1} t3`,
      group: "Body",
      material: "cast",
      mesh: wedgeMesh([-1.15, -1.0], [-1.15, 0.55], [0.55, -1.0], 0.22, tr([0, 0, z])),
    });
  });
  add({
    id: "web_stiffener",
    entity: "part.body.web_stiffener",
    label: "Web Stiffener (rear)",
    group: "Body",
    material: "cast",
    mesh: wedgeMesh([-0.6, -1.0], [-0.6, 0.5], [1.4, -1.0], 0.2, tr([-1.7, 0, 0], [0, H, 0])),
  });
  add({
    id: "bearing_boss",
    entity: "part.body.bearing_boss",
    label: "Bearing Boss Ø20.0",
    group: "Body",
    material: "machined",
    mesh: tubeMesh(1.0, 0.62, 0.55, q(36), axisXform("x", [-1.15, 0.7, 0]), true, false, cs(0.6)),
  });
  add({
    id: "bore_collar",
    entity: "part.body.bore_collar",
    label: "Bore Chamfer Collar",
    group: "Body",
    material: "steel",
    mesh: tubeMesh(1.12, 0.62, 0.14, q(36), axisXform("x", [-0.6, 0.7, 0]), true, false, cs(0.6)),
  });

  const feet: [number, number][] = [
    [-1.35, -1.0],
    [1.35, -1.0],
    [-1.35, 1.0],
    [1.35, 1.0],
  ];
  feet.forEach(([px, pz], i) => {
    add({
      id: `mount_bushing_${i}`,
      entity: `assy.mount_bushing_0${i + 1}`,
      label: `Mount Bushing 0${i + 1}`,
      group: "Fasteners",
      material: "machined",
      mesh: tubeMesh(0.32, 0.17, 0.22, q(18), tr([px, -1.0, pz]), true, false, cs(0.6)),
    });
  });

  add({
    id: "lift_lug",
    entity: "feat.lift_lug",
    label: "Lift Lug M8",
    group: "Features",
    material: "cast",
    mesh: boxMesh(0.45, 0.3, 1.2, tr([-1.375, 2.15, 0]), cs(0.4)),
  });
  add({
    id: "lug_bushing",
    entity: "feat.lug_bushing",
    label: "Lug Bushing Ø8",
    group: "Features",
    material: "anodized",
    mesh: tubeMesh(0.21, 0.11, 0.2, q(18), tr([-1.375, 2.3, 0]), true, false, cs(0.6)),
  });
  add({
    id: "id_plate",
    entity: "feat.id_plate",
    label: "Serial ID Plate",
    group: "Features",
    material: "anodized",
    mesh: boxMesh(0.05, 0.85, 1.5, tr([-1.625, 0.95, 0]), cs(0.4)),
  });

  return {
    id: "gbxl4",
    name: "Gearbox Bracket",
    code: "GBX-L4",
    standard: "ISO 2768-mK · ISO 1101",
    materialNote: "GG25 grey iron, stress relieved",
    mass: "6.55 kg",
    solids,
    gdt: [
      {
        id: "gdt_a",
        datum: "A",
        frame: "▱ 0.08 A",
        anchor: [1.35, -1.45, 1.0],
        elbow: [2.7, -1.9, 1.7],
        tail: [3.9, -1.9, 1.7],
      },
      {
        id: "gdt_b",
        datum: "B",
        frame: "◎ 0.03 A-B",
        anchor: [-0.6, 0.7, 0.62],
        elbow: [0.7, 1.75, 1.25],
        tail: [1.9, 1.75, 1.25],
      },
      {
        id: "gdt_c",
        datum: "C",
        frame: "⟂ 0.05 C",
        anchor: [-1.6, 1.6, -0.6],
        elbow: [-2.9, 2.5, -1.3],
        tail: [-4.0, 2.5, -1.3],
      },
    ],
    dims: [
      { a: [-2.1, -1.45, 1.8], b: [2.1, -1.45, 1.8], label: "42.00" },
      { a: [2.4, -1.45, -1.4], b: [2.4, 2.3, -1.4], label: "37.50" },
      { a: [-1.6, 0.7, 1.6], b: [-0.46, 0.7, 1.6], label: "11.40" },
    ],
    load: [-0.46, 0.7, 0],
    support: [0, -1.45, 0],
    home: { yaw: -0.72, pitch: 0.36, dist: 10.4, target: [-0.3, 0.3, 0] },
    sectionRange: [-2.2, 2.2],
  };
}

const BUILDERS: Record<string, () => PartDef> = {
  brg220: housing,
  mfl6b: flange,
  gbxl4: bracket,
};

export const PART_IDS = ["brg220", "mfl6b", "gbxl4"] as const;

const cache = new Map<string, PartDef>();

export function buildPart(id: string, lod = 1): PartDef {
  const key = `${id}@${lod}`;
  const hit = cache.get(key);
  if (hit) return hit;
  const prev = L;
  L = LOD_LEVELS[lod]?.factor ?? 1;
  const built = (BUILDERS[id] ?? housing)();
  L = prev;
  cache.set(key, built);
  return built;
}

export const PARTS: PartDef[] = PART_IDS.map((id) => buildPart(id, 1));
