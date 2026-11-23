import { useEffect, useRef, useState } from "react";
import type { ReactNode } from "react";
import {
  ArrowDownToLine,
  ArrowUpRight,
  BookOpen,
  Box,
  Check,
  ChevronRight,
  Code2,
  FileCode2,
  Layers3,
  ShieldCheck,
  X,
} from "lucide-react";
import type { Geometry, MeshData, MeshParams } from "./mesh";
import { GEOMETRIES } from "./mesh";

export function Logo() {
  return (
    <span className="brand">
      <svg viewBox="0 0 38 40" aria-hidden="true">
        <path d="m19 1 16 9-7.4 4.3L19 9.5l-8.7 4.9L3 10Z" fill="#9aeee1" />
        <path d="m3 12 7.3 4.2v10.2l8.7 5v8L3 30Z" fill="#61cdbd" />
        <path d="m35 12-7.4 4.2v10.2L19 31.3v8L35 30Z" fill="#8ae4d5" />
        <path d="m19 13 8 4.5-8 4.6-8-4.6Z" fill="#adf3e8" />
        <path d="m11 17.5 8 4.6v9l-8-4.6Z" fill="#3c9d91" />
        <path d="m27 17.5-8 4.6v9l8-4.6Z" fill="#68c4b7" />
      </svg>
      <span>HexaLab</span>
    </span>
  );
}
export function Toggle({
  value,
  onChange,
  label,
  disabled = false,
}: {
  value: boolean;
  onChange: (v: boolean) => void;
  label: string;
  disabled?: boolean;
}) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={value}
      aria-label={label}
      disabled={disabled}
      className={`toggle ${value ? "on" : ""}`}
      onClick={() => onChange(!value)}
    >
      <span />
    </button>
  );
}
export function Modal({
  title,
  eyebrow,
  children,
  onClose,
  wide = false,
}: {
  title: string;
  eyebrow: string;
  children: ReactNode;
  onClose: () => void;
  wide?: boolean;
}) {
  const ref = useRef<HTMLDivElement>(null),
    close = useRef(onClose);
  close.current = onClose;
  useEffect(() => {
    const previous = document.activeElement as HTMLElement,
      old = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    ref.current?.focus();
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") close.current();
      if (e.key === "Tab") {
        const els = ref.current?.querySelectorAll<HTMLElement>(
          'button:not(:disabled), a[href], select, input, [tabindex="0"]',
        );
        if (!els?.length) return;
        const first = els[0],
          last = els[els.length - 1];
        if (
          e.shiftKey &&
          (document.activeElement === first ||
            document.activeElement === ref.current)
        ) {
          last.focus();
          e.preventDefault();
        } else if (!e.shiftKey && document.activeElement === last) {
          first.focus();
          e.preventDefault();
        }
      }
    };
    document.addEventListener("keydown", onKey);
    return () => {
      document.body.style.overflow = old;
      document.removeEventListener("keydown", onKey);
      previous?.focus();
    };
  }, []);
  return (
    <div
      className="modal-backdrop"
      onMouseDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <div
        className={`modal ${wide ? "modal-wide" : ""}`}
        ref={ref}
        role="dialog"
        aria-modal="true"
        aria-label={title}
        tabIndex={-1}
      >
        <div className="modal-header">
          <div>
            <span className="eyebrow">{eyebrow}</span>
            <h2>{title}</h2>
          </div>
          <button
            className="icon-button"
            onClick={onClose}
            aria-label="Close dialog"
          >
            <X size={18} />
          </button>
        </div>
        {children}
      </div>
    </div>
  );
}
export function GeometryPreview({ geometry }: { geometry: Geometry }) {
  const id = geometry ? "plate-fig" : "l-fig";
  return (
    <svg className="geometry-preview" viewBox="0 0 220 96" aria-hidden="true">
      <defs>
        <pattern
          id={id}
          width="8"
          height="8"
          patternUnits="userSpaceOnUse"
          patternTransform="matrix(1 .38 .82 -.54 0 0)"
        >
          <path d="M0 0H8V8" fill="none" stroke="#263e58" strokeWidth="1.25" />
        </pattern>
      </defs>
      {geometry ? (
        <>
          <path d="m35 46 66-30 88 30-63 31Z" fill="#8ba5c9" />
          <path d="m35 46 91 31v7L35 53Z" fill="#4b728b" />
          <path d="m126 77 63-31v7l-63 31Z" fill="#45647b" />
          <path d="m35 46 66-30 88 30-63 31Z" fill={`url(#${id})`} />
        </>
      ) : (
        <>
          <path
            d="m42 57 29-14v-26l35-14 21 9v24l50 19-32 24Z"
            fill="#607fa7"
          />
          <path d="m42 57 29-14v-26l20 8v26l54 21v16Z" fill="#7698d0" />
          <path d="m71 17 35-14 21 9-36 13Z" fill="#a4b8d5" />
          <path d="m91 25 36-13v24L91 51Z" fill="#476d99" />
          <path d="m91 51 36-15 50 19-32 17Z" fill="#95add1" />
          <path d="m145 72 32-17v16l-32 17Z" fill="#537390" />
          <path
            d="m42 57 29-14v-26l35-14 21 9v24l50 19v16l-32 17-103-31Z"
            fill={`url(#${id})`}
          />
        </>
      )}
    </svg>
  );
}
export function GeometryLibrary({
  current,
  onSelect,
  onClose,
}: {
  current: Geometry;
  onSelect: (g: Geometry) => void;
  onClose: () => void;
}) {
  return (
    <Modal
      title="Mesh library"
      eyebrow="REFERENCE GEOMETRIES"
      onClose={onClose}
      wide
    >
      <p className="modal-intro">
        Open a verified benchmark in the inspection workspace.
      </p>
      <div className="library-grid">
        {([0, 1] as Geometry[]).map((g) => (
          <button
            key={g}
            className={`geometry-option ${current === g ? "current" : ""}`}
            onClick={() => onSelect(g)}
          >
            <div className="geometry-image">
              <GeometryPreview geometry={g} />
              <span className="mono">0{g + 1}</span>
            </div>
            <div className="geometry-description">
              <span className="eyebrow">
                {g ? "NARROW CLEARANCE" : "CONCAVE GEOMETRY"}
              </span>
              <h3>{GEOMETRIES[g]}</h3>
              <p>
                {g
                  ? "Opposed-wall extrusion in a 5 mm cooling gap."
                  : "A stepped channel with sharp, reentrant corners."}
              </p>
              <div>
                <span className="mono">
                  {g ? "80 x 32 x 5" : "80 x 32 x 40"} mm
                </span>
                {current === g ? (
                  <span className="text-mint">
                    <Check size={13} /> Open
                  </span>
                ) : (
                  <ChevronRight size={15} />
                )}
              </div>
            </div>
          </button>
        ))}
      </div>
      <div className="modal-footnote">
        <ShieldCheck size={14} /> Both fixtures are validated by the C++20
        engine.
      </div>
    </Modal>
  );
}
export function Parameters({
  mesh,
  onBuild,
  onClose,
}: {
  mesh: MeshData;
  onBuild: (p: MeshParams) => void;
  onClose: () => void;
}) {
  const [p, setP] = useState<MeshParams>({ ...mesh.params });
  return (
    <Modal
      title="Meshing parameters"
      eyebrow="BOUNDARY-LAYER ENGINE"
      onClose={onClose}
    >
      <p className="modal-intro">
        Rebuild {GEOMETRIES[mesh.geometry].toLowerCase()} using the C++20 engine
        in your browser.
      </p>
      <form
        onSubmit={(e) => {
          e.preventDefault();
          onBuild(p);
        }}
      >
        <div className="parameter-form">
          <label>
            Prism layers <span>Number of advancing layers</span>
            <input
              type="number"
              min="1"
              max="12"
              step="1"
              required
              value={p.layers}
              onChange={(e) => setP({ ...p, layers: +e.target.value })}
            />
          </label>
          <label>
            First-layer height <span>Requested height in millimeters</span>
            <input
              type="number"
              min="0.015"
              max="2"
              step="0.001"
              required
              value={p.first}
              onChange={(e) => setP({ ...p, first: +e.target.value })}
            />
          </label>
          <label>
            Growth ratio <span>Thickness multiplier per layer</span>
            <input
              type="number"
              min="1"
              max="1.6"
              step="0.01"
              required
              value={p.growth}
              onChange={(e) => setP({ ...p, growth: +e.target.value })}
            />
          </label>
        </div>
        <div className="method-note">
          <ShieldCheck size={17} />
          <p>
            <strong>Quality gates remain active.</strong> Local retraction
            limits thickness to 40% of opposing-wall clearance. A failed mesh is
            never committed.
          </p>
        </div>
        <div className="modal-buttons">
          <button type="button" className="secondary-button" onClick={onClose}>
            Cancel
          </button>
          <button className="primary-button">
            <Layers3 size={15} />
            Rebuild mesh
          </button>
        </div>
      </form>
    </Modal>
  );
}
const DOC_TABS = [
  { name: "Workspace guide", icon: BookOpen },
  { name: "Numerical methods", icon: Layers3 },
  { name: "Verification scope", icon: ShieldCheck },
  { name: "Build locally", icon: Code2 },
];
export function Documentation({
  onClose,
  initial = 0,
}: {
  onClose: () => void;
  initial?: number;
}) {
  const [tab, setTab] = useState(initial);
  return (
    <Modal
      title="Documentation"
      eyebrow="HEXALAB / KNOWLEDGE BASE"
      onClose={onClose}
      wide
    >
      <div className="docs-layout">
        <nav>
          {DOC_TABS.map(({ name, icon: Icon }, i) => (
            <button
              key={name}
              onClick={() => setTab(i)}
              className={i === tab ? "selected" : ""}
            >
              <Icon size={15} />
              {name}
            </button>
          ))}
        </nav>
        <article className="docs-content">
          <div className="doc-index">0{tab + 1} / 04</div>
          <h3>{DOC_TABS[tab].name}</h3>
          {tab === 0 ? (
            <>
              <p>
                Inspect a real mixed-element volume mesh. Both reference
                geometries are bundled with the workspace, with no remote solver
                required.
              </p>
              <h4>Navigate the volume</h4>
              <p>
                Drag to orbit. Shift-drag or use the hand tool to pan. Scroll to
                zoom, and use the fit control to return to the isometric view.
              </p>
              <h4>Inspect a section</h4>
              <p>
                Choose a plane normal and drag the position slider. The cut
                polygons intersect the actual element edges. Use the arrow tool
                to pick a cell.
              </p>
              <h4>Recompute and export</h4>
              <p>
                Run verification rebuilds and tests every cell in a dedicated
                WebAssembly worker. Export a VTK mesh for ParaView, a JSON
                report, or a selected finite-area wall mesh.
              </p>
            </>
          ) : tab === 1 ? (
            <>
              <p>
                Angle-weighted inward normals are projected into the incident
                visibility cones, then smoothed with a constrained surface-graph
                Laplacian.
              </p>
              <h4>Clearance and extrusion</h4>
              <p>
                A triangle BVH tests five rays per vertex. Local heights are
                capped by the opposing wall and a mesh-size bound. A
                quadratic-in-height wedge determinant test checks the full
                prism, not just its corners.
              </p>
              <h4>Conformal transitions</h4>
              <p>
                Stromberg's 23 canonical templates expand to 256 cube masks.
                Residual voting uses 125 integration points and the cost
                missing² + excess. The aligned fixtures use the full mask. A
                shared-face pyramid/tet fan joins the triangular prism front to
                the hex core.
              </p>
              <h4>Mixed-element quality</h4>
              <p>
                Scaled Jacobians use type-specific optima. Pyramid bases receive
                a fictitious-hexahedron test to reject folded and concave
                quadrilaterals.
              </p>
              <a
                href="https://arxiv.org/abs/2307.08825"
                target="_blank"
                rel="noreferrer"
              >
                Stromberg et al. reference <ArrowUpRight size={13} />
              </a>
            </>
          ) : tab === 2 ? (
            <>
              <p>
                These results are numerical certificates for the two supplied,
                grid-aligned fixtures, not a proof for arbitrary industrial CAD.
              </p>
              <h4>Acceptance criteria</h4>
              <p>
                Every signed volume is positive, every JENS exceeds 0.25, and
                the largest normalized Gauss residual is below 10⁻¹⁴. Interior
                faces must have two oppositely oriented owners; unmatched faces
                must be the known exterior.
              </p>
              <h4>Geometric limits</h4>
              <p>
                Both shipped exports also pass an independent exact-integer audit
                of their signed simplex volumes, Jacobian thresholds, continuous
                prism bounds, and zero area-vector sums. Interactive rebuilds
                run the binary64 acceptance checks.
              </p>
              <a href="/meshes/certificates.json" download>
                Reference certificates <ArrowDownToLine size={13} />
              </a>
              <h4>Supported geometry</h4>
              <p>
                The L-channel has a two-level 2:1 Cartesian core. Coarse face
                and edge subdivisions are shared with fine neighbors. The thin
                gap remains uniform. General BREP fitting and arbitrary-depth
                octrees are outside this fixture engine. Ray sampling alone does
                not prove collision freedom. Positive simplex fans, exterior
                matching, and analytic volume agreement are checked together.
              </p>
              <h4>Finite-area coupling</h4>
              <p>
                All wall faces form a closed manifold. An individual wall patch
                has boundary edges. Face-to-owner indices are unique for these
                extruded fixtures, not necessarily for a general polyhedral
                mesh.
              </p>
            </>
          ) : (
            <>
              <p>
                The standalone engine uses only the C++20 standard library. The
                archive includes source, canonical tables, the reference
                license, and reproducible tests.
              </p>
              <h4>Compile and test</h4>
              <pre>
                g++ -std=c++20 -O2 octree_hexcore.cpp -o hexalab{"\n"}./hexalab
                --self-test
              </pre>
              <h4>Generate a fixture</h4>
              <pre>
                ./hexalab --geometry l-channel \{"\n"} --layers 6 --first-height
                0.18 \{"\n"} --growth 1.22 --out mesh.json
              </pre>
              <a
                className="secondary-button"
                download
                href="/engine/hexalab-source.zip"
              >
                <ArrowDownToLine size={14} />
                Download C++20 source
              </a>
            </>
          )}
        </article>
      </div>
      <div className="modal-footnote">
        <FileCode2 size={14} /> octree_hexcore.cpp{" "}
        <span className="ml-auto mono">Engine 1.0.0</span>
      </div>
    </Modal>
  );
}
export function LoadingView({
  error,
  retry,
}: {
  error: string | null;
  retry: () => void;
}) {
  return (
    <div className="loading-view">
      <Box className={error ? "" : "loading-cube"} size={35} />
      <h2>{error ? "Unable to open mesh" : "Preparing workspace"}</h2>
      <p>{error || "Loading verified volume geometry..."}</p>
      {error && (
        <button className="secondary-button" onClick={retry}>
          Retry loading
        </button>
      )}
    </div>
  );
}
