import { Chip, StatBlock } from "./ui";
import { Logo } from "./Sidebar";

const KPIS = [
  { value: "65 %", label: "weniger VRAM-Traffic", sub: "bei 95 % orthogonalen Faces · Modell 96·N₀/(140·N)", tone: "cyan" as const },
  { value: "128³", label: "Pilot-Gitter in 0,2 s", sub: "LBM/VDB-Vorlauf liefert h(x) und das Startfeld", tone: "violet" as const },
  { value: "4", label: "Speicher-Segmente", sub: "inner/halo × orthogonal/korrigiert, physisch getrennt", tone: "emerald" as const },
  { value: "128", label: "Cloud-Nodes", sub: "Hilbert-Split + Inner/Halo-Overlap + s-Step", tone: "amber" as const },
];

export function Hero() {
  return (
    <header id="ueberblick" className="relative overflow-hidden px-5 pt-14 pb-16 sm:px-8 lg:px-14">
      <div className="mx-auto max-w-6xl">
        <div className="flex flex-col gap-10 lg:flex-row lg:items-start lg:gap-14">
          <div className="min-w-0 flex-1">
            <div className="mb-6 hidden lg:block">
              <Logo className="h-11 w-11" />
            </div>

            <div className="mb-5 flex flex-wrap gap-2">
              <Chip tone="cyan">Kokkos · CUDA · HIP · SYCL</Chip>
              <Chip tone="violet">Enzyme Reverse-AD</Chip>
              <Chip tone="emerald">preCICE + MUI</Chip>
              <Chip tone="amber">MeshErrorMap v2</Chip>
            </div>

            <h1 className="text-[2rem] leading-[1.1] font-semibold tracking-tight text-white sm:text-[2.75rem]">
              Ein CFD-/Multiphysics-Solver, der{" "}
              <span className="bg-gradient-to-r from-cyan-300 via-sky-300 to-violet-300 bg-clip-text text-transparent">
                weiß, wo sein Gitter schlecht ist
              </span>
              .
            </h1>

            <p className="mt-5 max-w-2xl text-[15px] leading-relaxed text-slate-400">
              Dieser Hub bündelt die Architektur-Elemente für eine GPU-beschleunigte
              Simulationsplattform: Pilot-Meshing aus einem LBM/Vorb-Vorlauf, eine
              MeshErrorMap als single source of truth, daraus abgeleitetes
              Zwei-Klassen-Kernel-Dispatching, Enzyme-Adjoints für Optimierung und
              Gittermorphing, RT-Core-Beschleunigung sowie Hilbert-Partitionierung für
              Multi-Node-Cloud-Betrieb.
            </p>

            <div className="mt-7 flex flex-wrap items-center gap-3">
              <a
                href="#errormap"
                className="rounded-xl bg-cyan-400 px-4 py-2.5 text-sm font-semibold text-[#06202a] transition-colors hover:bg-cyan-300"
              >
                MeshErrorMap interaktiv erkunden
              </a>
              <a
                href="#ideen"
                className="rounded-xl border border-white/15 bg-white/5 px-4 py-2.5 text-sm font-medium text-slate-200 transition-colors hover:border-white/30 hover:bg-white/10"
              >
                12 neue Ideen ansehen
              </a>
            </div>
          </div>

          <div className="grid w-full shrink-0 grid-cols-2 gap-3 lg:w-[380px]">
            {KPIS.map((k) => (
              <StatBlock key={k.label} value={k.value} label={k.label} sub={k.sub} tone={k.tone} />
            ))}
          </div>
        </div>

        <div className="mt-12 grid gap-3 sm:grid-cols-3">
          <Principle
            title="Blindheit des Meshers aufheben"
            text="Der Mesher erfährt vor dem Vernetzen, wo Scherschichten, Ablösung und Nachlaufwirbel entstehen — und verfeinert dort schon im ersten Netz."
          />
          <Principle
            title="Gitterqualität ist ein Datenpfad"
            text="Skewness, Nicht-Orthogonalität und Dämpfung sind keine Diagnose, sondern steuern Speicherlayout, Kernel-Dispatch, Löser und Zeitschritt."
          />
          <Principle
            title="Geschlossener Lernkreislauf"
            text="Adjoint-Fehleranalyse → Metrik-Predictor → Netz → Simulation. Jedes gerechnete Bauteil ist ein Trainingspaar."
          />
        </div>
      </div>
    </header>
  );
}

function Principle({ title, text }: { title: string; text: string }) {
  return (
    <div className="rounded-2xl border border-white/10 bg-gradient-to-b from-white/[0.05] to-transparent p-5">
      <div className="mb-2 flex items-center gap-2">
        <span className="h-1.5 w-1.5 rounded-full bg-cyan-400" />
        <h3 className="text-sm font-semibold text-white">{title}</h3>
      </div>
      <p className="text-[12.5px] leading-relaxed text-slate-400">{text}</p>
    </div>
  );
}
