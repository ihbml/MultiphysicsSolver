import React, { createContext, useCallback, useContext, useMemo, useRef, useState } from "react";
import {
  FIELDS, FieldKey, KPIS, PATCHES, Patch, RuleSeed, RULES_SEED, patchById,
  synthesizeRule, SynthRule,
} from "./lib/sim";
import { V3 } from "./lib/math3d";

export type Workspace = "rules" | "domain" | "probe" | "compare" | "calc";
export type Rule = RuleSeed;
export type ActorKind = "injector" | "mic" | "porous";

export type Injector = { id: string; name: string; pos: V3; axis: V3; cone: number; d32: number; spread: number; enabled: boolean };
export type Mic = { id: string; name: string; pos: V3; radius: number; enabled: boolean; dbspl: number };
export type Porous = { id: string; name: string; pos: V3; e1: V3; e2: V3; e3: V3; k1: number; k2: number; k3: number; enabled: boolean };

export type DerivedField = { name: string; unit: string; dims: string; quantity: string; min: number; max: number };

type ProbeState = {
  kind: "line" | "chord";
  a: V3;
  b: V3;
  samples: number;
  field: FieldKey;
  invertY: boolean;
  targetBands: boolean;
  lawWall: boolean;
  runB: boolean;
  followSurface: boolean;
};

type DomainState = {
  upstream: number;
  downstream: number;
  radial: number;
  showDomain: boolean;
  showMrf: boolean;
  showMesh: boolean;
  rpm: number;
  mrfRadius: number;
  mrfHeight: number;
  bladeCount: number;
  slice: boolean;
  sliceY: number;
  axis: "x" | "y" | "z";
  cells: number;
};

type CompareState = {
  mode: "wipe" | "split" | "delta";
  wipe: number;
  slice: boolean;
  sliceX: number;
  field: FieldKey;
  showContours: boolean;
  sync: boolean;
};

type Ctx = {
  workspace: Workspace;
  setWorkspace: (w: Workspace) => void;
  activeField: FieldKey | "custom";
  setActiveField: (f: FieldKey | "custom") => void;
  hoveredPatch: string | null;
  setHoveredPatch: (p: string | null) => void;
  selectedPatch: Patch;
  selectPatch: (id: string) => void;
  synth: SynthRule;
  rules: Rule[];
  selectedRule: string | null;
  setSelectedRule: (id: string | null) => void;
  toggleRule: (id: string) => void;
  deleteRule: (id: string) => void;
  updateRuleExpression: (id: string, expr: string) => void;
  mergeDraft: () => void;
  separateDraft: () => void;
  conflicts: Set<string>;
  conflictRules: Set<string>;
  toast: string | null;
  setToast: (t: string | null) => void;
  notify: (t: string) => void;
  domain: DomainState;
  setDomain: React.Dispatch<React.SetStateAction<DomainState>>;
  targetL: number;
  tipMach: number;
  tipSpeed: number;
  injectors: Injector[];
  mics: Mic[];
  porous: Porous[];
  actorsVisible: boolean;
  setActorsVisible: (v: boolean) => void;
  addActor: (kind: ActorKind, pos?: V3) => void;
  updateInjector: (id: string, p: Partial<Injector>) => void;
  updateMic: (id: string, p: Partial<Mic>) => void;
  updatePorous: (id: string, p: Partial<Porous>) => void;
  removeActor: (kind: ActorKind, id: string) => void;
  selectedActor: { kind: ActorKind; id: string } | null;
  setSelectedActor: (v: { kind: ActorKind; id: string } | null) => void;
  probe: ProbeState;
  setProbe: React.Dispatch<React.SetStateAction<ProbeState>>;
  compare: CompareState;
  setCompare: React.Dispatch<React.SetStateAction<CompareState>>;
  viewPreset: { name: string; n: number };
  setViewPreset: (name: string) => void;
  derivedFields: DerivedField[];
  addDerivedField: (f: DerivedField) => void;
  calcExpr: string;
  setCalcExpr: React.Dispatch<React.SetStateAction<string>>;
  uploaded: DerivedField | null;
  setUploaded: (f: DerivedField | null) => void;
  camera: React.MutableRefObject<{ azimuth: number; elevation: number; distance: number; target: V3 }>;
  cameraVersion: number;
  bumpCamera: () => void;
  showHelp: boolean;
  setShowHelp: (v: boolean) => void;
  hideUi: boolean;
  setHideUi: (v: boolean) => void;
};

const CockpitContext = createContext<Ctx | null>(null);

export const initialDomain: DomainState = {
  upstream: 5, downstream: 15, radial: 5, showDomain: true, showMrf: true, showMesh: false,
  rpm: 12400, mrfRadius: 1.24, mrfHeight: 1.62, bladeCount: 48, slice: false, sliceY: 0.18,
  axis: "y", cells: 8_412_096,
};

export function CockpitProvider({ children }: { children: React.ReactNode }) {
  const [workspace, setWorkspace] = useState<Workspace>("rules");
  const [activeField, setActiveField] = useState<FieldKey | "custom">("Cp");
  const [hoveredPatch, setHoveredPatch] = useState<string | null>(null);
  const [selectedPatchId, setSelectedPatchId] = useState("SS");
  const [rules, setRules] = useState<Rule[]>(RULES_SEED.map((r) => ({ ...r })));
  const [selectedRule, setSelectedRule] = useState<string | null>("R4");
  const [toast, setToast] = useState<string | null>(null);
  const [domain, setDomain] = useState<DomainState>(initialDomain);
  const [injectors, setInjectors] = useState<Injector[]>([
    { id: "INJ-01", name: "Primary pilot", pos: [-1.34, 0.34, 0.44], axis: [0.86, -0.12, -0.5], cone: 22, d32: 38, spread: 1.8, enabled: true },
    { id: "INJ-02", name: "Main stage A", pos: [-1.18, -0.18, -0.66], axis: [0.92, 0.08, 0.38], cone: 31, d32: 54, spread: 2.4, enabled: true },
  ]);
  const [mics, setMics] = useState<Mic[]>([{ id: "MIC-01", name: "Farfield mic 1", pos: [-1.9, 1.48, 1.42], radius: 0.34, enabled: true, dbspl: 121.4 }]);
  const [porous, setPorous] = useState<Porous[]>([{
    id: "POM-01", name: "Trailing-edge foam", pos: [1.22, 0.24, -0.28],
    e1: [0.96, 0.1, 0.25], e2: [0.06, 0.42, -0.9], e3: [0.26, -0.9, -0.34],
    k1: 2.4e-9, k2: 1.2e-8, k3: 4.1e-8, enabled: true,
  }]);
  const [actorsVisible, setActorsVisible] = useState(true);
  const [selectedActor, setSelectedActor] = useState<{ kind: ActorKind; id: string } | null>(null);
  const [probe, setProbe] = useState<ProbeState>({
    kind: "line", a: [-1.12, 0.44, 0.62], b: [1.05, 0.08, -0.36], samples: 64,
    field: "Cp", invertY: true, targetBands: true, lawWall: true, runB: true, followSurface: false,
  });
  const [compare, setCompare] = useState<CompareState>({
    mode: "wipe", wipe: 0.52, slice: false, sliceX: 0.12, field: "wallT", showContours: true, sync: true,
  });
  const [viewPreset, setViewPreset] = useState({ name: "Hero 3/4", n: 0 });
  const [derivedFields, setDerivedFields] = useState<DerivedField[]>([]);
  const [calcExpr, setCalcExpr] = useState("p_total = p + 0.5 * rho * dot(U, U)");
  const [uploaded, setUploaded] = useState<DerivedField | null>(null);
  const [showHelp, setShowHelp] = useState(false);
  const [hideUi, setHideUi] = useState(false);
  const camera = useRef({ azimuth: -0.72, elevation: 0.34, distance: 6.4, target: [0.12, 0.02, 0.04] as V3 });
  const [cameraVersion, setCameraVersion] = useState(0);
  const bumpCamera = useCallback(() => setCameraVersion((v) => v + 1), []);
  const toastTimer = useRef<number | null>(null);

  const notify = useCallback((t: string) => {
    setToast(t);
    if (toastTimer.current) window.clearTimeout(toastTimer.current);
    toastTimer.current = window.setTimeout(() => setToast(null), 4200);
  }, []);

  const selectedPatch = patchById(selectedPatchId);
  const synth = useMemo(() => synthesizeRule(selectedPatch), [selectedPatch]);

  const patchRuleMap = useMemo(() => {
    const map = new Map<string, string[]>();
    rules.forEach((r) => {
      if (!r.active) return;
      r.patches.forEach((p) => map.set(p, [...(map.get(p) || []), r.id]));
    });
    return map;
  }, [rules]);

  const conflicts = useMemo(
    () => new Set([...patchRuleMap.entries()].filter(([, ids]) => ids.length > 1).map(([p]) => p)),
    [patchRuleMap]
  );
  const conflictRules = useMemo(
    () => new Set([...conflicts].flatMap((p) => patchRuleMap.get(p) || [])),
    [conflicts, patchRuleMap]
  );

  const selectPatch = useCallback((id: string) => {
    setSelectedPatchId(id);
    const p = patchById(id);
    notify(`${p.short} picked · R32Uint face_group_id = ${p.group} · ${p.faces.toLocaleString()} faces`);
  }, [notify]);

  const mergeDraft = useCallback(() => {
    const p = selectedPatch;
    if (p.kind !== "wall") {
      notify(`${p.short} is a ${p.kind} boundary — create a dedicated rule instead of merging it into a wall BC`);
      return;
    }
    setRules((rs) => rs.map((r) => {
      if (r.kind !== "wall" || !r.patches.some((x) => ["SS", "PS", "LE", "TE", "TIP"].includes(x))) return r;
      return { ...r, patches: r.patches.includes(p.id) ? r.patches : [...r.patches, p.id], expression: synth.expression, synthesized: true };
    }));
    setSelectedRule("R3");
    notify(`Merged ${p.short} into “Blade · adiabatic no-slip” · ${p.faces.toLocaleString()} faces matched`);
  }, [selectedPatch, synth, notify]);

  const separateDraft = useCallback(() => {
    const p = selectedPatch;
    const id = `R${Math.floor(Math.random() * 800 + 100)}`;
    const name = `${p.short} · synthesized ${p.kind === "wall" ? "wall" : p.kind}`;
    setRules((rs) => [...rs, {
      id, name, kind: p.kind, patches: [p.id], active: true, expression: synth.expression,
      target: synth.human, synthesized: true, color: "#4D7CFF", faces: p.faces,
    }]);
    setSelectedRule(id);
    if (patchRuleMap.get(p.id)?.length) notify(`⚠ Overlap detected: ${p.short} is already bound · resolved in red`);
    else notify(`Rule synthesized from ${p.short} · topology-independent · ${synth.matchedFaces.toLocaleString()} faces`);
  }, [selectedPatch, synth, notify, patchRuleMap]);

  const addActor = useCallback((kind: ActorKind, pos?: V3) => {
    const suffix = String(Math.floor(Math.random() * 90 + 10));
    if (kind === "injector") {
      const id = `INJ-${suffix}`;
      setInjectors((a) => [...a, { id, name: "DPM injector", pos: pos ?? [-0.9, 0.3, 0.5], axis: [0.9, -0.1, -0.4], cone: 26, d32: 45, spread: 2.1, enabled: true }]);
      setSelectedActor({ kind, id });
      notify(`Injector ${id} placed · apex snapped to surface · ${pos ? pos.map((v) => v.toFixed(2)).join(", ") : "viewport drop"}`);
    } else if (kind === "mic") {
      const id = `MIC-${suffix}`;
      setMics((a) => [...a, { id, name: "FWH microphone", pos: pos ?? [-1.6, 1.2, 1.4], radius: 0.3, enabled: true, dbspl: 118.2 }]);
      setSelectedActor({ kind, id });
      notify(`FWH microphone ${id} added · observer sphere r = 0.30 m`);
    } else {
      const id = `POM-${suffix}`;
      setPorous((a) => [...a, { id, name: "Porous region", pos: pos ?? [1.1, 0.2, -0.2], e1: [0.9, 0.2, 0.36], e2: [-0.2, 0.94, -0.28], e3: [-0.36, -0.25, 0.9], k1: 2.4e-9, k2: 1.2e-8, k3: 4.1e-8, enabled: true }]);
      setSelectedActor({ kind, id });
      notify(`Anisotropic porous zone ${id} created · principal axes e₁e₂e₃ orthogonalized`);
    }
    setActorsVisible(true);
  }, [notify]);

  const updateInjector = useCallback((id: string, p: Partial<Injector>) => setInjectors((a) => a.map((x) => (x.id === id ? { ...x, ...p } : x))), []);
  const updateMic = useCallback((id: string, p: Partial<Mic>) => setMics((a) => a.map((x) => (x.id === id ? { ...x, ...p } : x))), []);
  const updatePorous = useCallback((id: string, p: Partial<Porous>) => setPorous((a) => a.map((x) => (x.id === id ? { ...x, ...p } : x))), []);
  const removeActor = useCallback((kind: ActorKind, id: string) => {
    if (kind === "injector") setInjectors((a) => a.filter((x) => x.id !== id));
    if (kind === "mic") setMics((a) => a.filter((x) => x.id !== id));
    if (kind === "porous") setPorous((a) => a.filter((x) => x.id !== id));
    setSelectedActor(null);
    notify(`${id} removed from scene`);
  }, [notify]);

  const toggleRule = useCallback((id: string) => setRules((rs) => rs.map((r) => (r.id === id ? { ...r, active: !r.active } : r))), []);
  const deleteRule = useCallback((id: string) => {
    setRules((rs) => rs.filter((r) => r.id !== id));
    setSelectedRule(null);
    notify(`Boundary rule ${id} removed`);
  }, [notify]);
  const updateRuleExpression = useCallback((id: string, expr: string) => setRules((rs) => rs.map((r) => (r.id === id ? { ...r, expression: expr } : r))), []);
  const setViewPresetByName = useCallback((name: string) => setViewPreset((v) => ({ name, n: v.n + 1 })), []);
  const addDerivedField = useCallback((f: DerivedField) => {
    setDerivedFields((d) => (d.some((x) => x.name === f.name) ? d.map((x) => (x.name === f.name ? f : x)) : [...d, f]));
    setUploaded(f);
    setActiveField("custom");
    notify(`${f.name} uploaded to CFDFLD03 · ${f.dims} · ${f.unit}`);
  }, [notify]);

  const targetL = 1.72;
  const tipRadius = domain.mrfRadius * 0.25;
  const tipSpeed = (2 * Math.PI * domain.rpm * tipRadius) / 60;
  const tipMach = tipSpeed / Math.sqrt(1.4 * 287.05 * 892);

  const value: Ctx = {
    workspace, setWorkspace, activeField, setActiveField, hoveredPatch, setHoveredPatch,
    selectedPatch, selectPatch, synth, rules, selectedRule, setSelectedRule, toggleRule, deleteRule,
    updateRuleExpression, mergeDraft, separateDraft, conflicts, conflictRules,
    toast, setToast, notify, domain, setDomain, targetL, tipMach, tipSpeed,
    injectors, mics, porous, actorsVisible, setActorsVisible, addActor,
    updateInjector, updateMic, updatePorous, removeActor, selectedActor, setSelectedActor,
    probe, setProbe, compare, setCompare, viewPreset, setViewPreset: setViewPresetByName,
    derivedFields, addDerivedField, calcExpr, setCalcExpr, uploaded, setUploaded,
    camera, cameraVersion, bumpCamera, showHelp, setShowHelp, hideUi, setHideUi,
  };

  return <CockpitContext.Provider value={value}>{children}</CockpitContext.Provider>;
}

export function useCockpit() {
  const ctx = useContext(CockpitContext);
  if (!ctx) throw new Error("useCockpit must be used inside CockpitProvider");
  return ctx;
}

export { KPIS, FIELDS, PATCHES };
