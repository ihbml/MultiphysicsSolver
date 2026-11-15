import { Chip } from "./ui";

type Accel = {
  title: string;
  hardware: string;
  text: string;
  gain: string;
  tone: "cyan" | "violet" | "emerald" | "amber";
};

const ACCELERATORS: Accel[] = [
  {
    title: "Hochfrequenz-EMF per SBR",
    hardware: "RT-Cores · OptiX / Embree",
    text: "Shooting-and-Bouncing-Rays für Radar-Rückstreuung, Antennen und Mikrowellen-Reaktoren: Millionen elektromagnetischer Strahlen inkl. Polarisation, Brechung und Beugung (UTD) — das Prinzip von HFSS SBR+.",
    gain: "statt Gitter-Sweeps",
    tone: "cyan",
  },
  {
    title: "Wärmestrahlung & Ablation",
    hardware: "RT-Cores · fvDOM / Monte Carlo",
    text: "Sichtfaktoren und Photonentransport für Strahlungs- und Hitzeschutzmodule werden auf der Hardware-BVH berechnet; Absorption an Wänden und Ablation koppeln direkt zurück.",
    gain: "20–100× schneller",
    tone: "amber",
  },
  {
    title: "Der Mesher strahlt selbst",
    hardware: "RT-Cores · CADMeshSDF",
    text: "Abstandsfragen zur CAD-Wand, Inside/Outside-Tests, Winding-Number-Auswertung und die Spaltmessung d_gap für Grenzschicht-Pinching sind 1:1 Strahltests — in Millisekunden.",
    gain: "Meshing in ms",
    tone: "violet",
  },
  {
    title: "Lagrange-Spray & Wandkollision",
    hardware: "RT-Cores · DPM",
    text: "Ob eine Million Brennstofftröpfchen im Schritt Δt auf eine Turbinenschaufel trifft, ist ein kurzer Strahl-Dreieck-Test pro Partikel — inklusive Filmbildung und Wandfilm-Rückführung.",
    gain: "1 Mio. Tests / Schritt",
    tone: "emerald",
  },
  {
    title: "Batch-Chemie auf Tensor Cores",
    hardware: "Tensor Cores · MMA",
    text: "Die steifen ODE-Jacobimatrizen J_chem ∈ R^{N_s×N_s} tausender Zellen lassen sich als dichte 16×16-Blöcke stapeln und direkt auf den Matrix-Multiply-Akkumulator-Einheiten invertieren.",
    gain: "4–8× reguläre Kerne",
    tone: "cyan",
  },
  {
    title: "Mixed-Precision-Defektkorrektur",
    hardware: "Tensor Cores · FP16 / TF32 + FP64",
    text: "ginkgo löst das Vorkonditionierungssystem M⁻¹r extrem schnell in FP16/TF32, während nur der Flussdefekt r = b − Ax exakt in FP64 berechnet wird.",
    gain: "Iteratives Refinement",
    tone: "violet",
  },
  {
    title: "Maxwell ohne Geistermoden",
    hardware: "Voronoi / DEC",
    text: "Auf einem primal-dualen Gitter — wo d_f exakt senkrecht auf S_f steht — liegt B auf den Flächen und E auf den Kanten. Das Yee-Schema bzw. Discrete Exterior Calculus hält ∇·B ≡ 0 bis auf Maschinengenauigkeit.",
    gain: "∇·B = 0 exakt",
    tone: "emerald",
  },
  {
    title: "Ballistischer Phononentransport",
    hardware: "RT-Cores · Boltzmann",
    text: "In mikroskopisch dünnen Keramik- oder Funktionsschichten (Kn > 0,1) versagt das Fouriersche Gesetz. Die Boltzmann-Transportgleichung für Gitterschwingungen lässt sich direkt auf der BVH lösen.",
    gain: "Sub-Mikron-Schichten",
    tone: "amber",
  },
];

const TONE = {
  cyan: "border-cyan-400/25 bg-cyan-400/[0.06]",
  violet: "border-violet-400/25 bg-violet-400/[0.06]",
  emerald: "border-emerald-400/25 bg-emerald-400/[0.06]",
  amber: "border-amber-400/25 bg-amber-400/[0.06]",
};

const TONE_TEXT = {
  cyan: "text-cyan-300",
  violet: "text-violet-300",
  emerald: "text-emerald-300",
  amber: "text-amber-300",
};

export function AcceleratorSection() {
  return (
    <div>
      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
        {ACCELERATORS.map((a) => (
          <div
            key={a.title}
            className={`flex flex-col gap-2.5 rounded-2xl border p-4 transition-colors hover:border-white/25 ${TONE[a.tone]}`}
          >
            <div className={`font-mono text-[10px] tracking-wide ${TONE_TEXT[a.tone]}`}>
              {a.hardware}
            </div>
            <h3 className="text-[13.5px] leading-snug font-semibold text-white">{a.title}</h3>
            <p className="text-[12px] leading-relaxed text-slate-400">{a.text}</p>
            <div className="mt-auto pt-1">
              <Chip tone={a.tone}>{a.gain}</Chip>
            </div>
          </div>
        ))}
      </div>

      <div className="mt-5 grid gap-3 md:grid-cols-2">
        <div className="rounded-2xl border border-white/10 bg-white/[0.02] p-5">
          <h3 className="mb-2 text-[13px] font-semibold text-white">
            EMF braucht ein anderes Netz als reines CFD
          </h3>
          <ul className="space-y-2 text-[12px] leading-relaxed text-slate-400">
            <li className="flex gap-2">
              <span className="mt-1.5 h-1 w-1 shrink-0 rounded-full bg-cyan-400" />
              <span>
                <span className="text-slate-200">Conformal Multi-Region:</span> Spule, Eisenkern,
                Fluid und umgebende Luft müssen gemeinsam vernetzt werden — der cht_coupled-Modus
                inklusive Luft-Box.
              </span>
            </li>
            <li className="flex gap-2">
              <span className="mt-1.5 h-1 w-1 shrink-0 rounded-full bg-amber-400" />
              <span>
                <span className="text-slate-200">Grenzschicht nach innen:</span> Der Skin-Effekt
                verdrängt den Strom ins Metall, δ_skin = 1/√(πfμσ). Es braucht dünne Prismenschichten
                in den Festkörper — spiegelbildlich zur y⁺-Grenzschicht im Fluid.
              </span>
            </li>
            <li className="flex gap-2">
              <span className="mt-1.5 h-1 w-1 shrink-0 rounded-full bg-violet-400" />
              <span>
                <span className="text-slate-200">Hohe Frequenz:</span> Sobald die Wellenlänge nicht
                mehr groß gegen die Zelle ist, helfen gekrümmte Kanten und p-Adaption mehr als
                beliebig viele Polyeder.
              </span>
            </li>
          </ul>
        </div>

        <div className="rounded-2xl border border-white/10 bg-white/[0.02] p-5">
          <h3 className="mb-2 text-[13px] font-semibold text-white">
            Kokkos / SYCL: ein Kernel, viele Architekturen
          </h3>
          <p className="text-[12px] leading-relaxed text-slate-400">
            Die Zwei-Klassen-Kernels werden portabel formuliert, damit derselbe Quelltext auf
            NVIDIA- (CUDA), AMD- (HIP) und Intel-GPUs (SYCL) sowie auf CPU-OpenMP läuft. Die
            Face-Partition bleibt dabei identisch — nur das Backend tauscht. Für die
            RT-Core-Pfade und die Tensor-Core-Chemie sind herstellerspezifische Spezialpfade
            vorgesehen, die über ein Feature-Flag zur Übersetzungszeit gewählt werden.
          </p>
          <div className="mt-3 flex flex-wrap gap-1.5">
            <Chip tone="cyan">CUDA</Chip>
            <Chip tone="emerald">HIP</Chip>
            <Chip tone="violet">SYCL</Chip>
            <Chip>OpenMP</Chip>
          </div>
        </div>
      </div>
    </div>
  );
}
