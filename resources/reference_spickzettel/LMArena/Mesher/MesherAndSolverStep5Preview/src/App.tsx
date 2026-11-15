import { AcceleratorSection } from "./components/AcceleratorSection";
import { NAV } from "./data/content";
import { useScrollProgress, useScrollSpy } from "./lib/useScrollSpy";
import { CodeSection } from "./components/CodeSection";
import { Footer } from "./components/Footer";
import { Hero } from "./components/Hero";
import { IdeasSection } from "./components/IdeasSection";
import { KernelTimeline } from "./components/KernelTimeline";
import { MeshErrorMapSection } from "./components/MeshErrorMapSection";
import { MobileNav, Sidebar } from "./components/Sidebar";
import { PipelineSection } from "./components/PipelineSection";
import { ScalingSection } from "./components/ScalingSection";
import { StackSection } from "./components/StackSection";
import { Section } from "./components/ui";

export default function App() {
  const active = useScrollSpy(NAV.map((n) => n.id));
  const progress = useScrollProgress();

  return (
    <div className="bg-grid relative min-h-screen">
      {/* Lesefortschritt */}
      <div className="fixed top-0 right-0 left-0 z-50 h-0.5 bg-transparent">
        <div
          className="h-full bg-gradient-to-r from-cyan-400 via-sky-400 to-violet-400 transition-[width] duration-150"
          style={{ width: `${progress * 100}%` }}
        />
      </div>

      <Sidebar active={active} />

      <main className="relative z-10 lg:pl-[272px]">
        <MobileNav active={active} />
        <Hero />

        <Section
          id="stack"
          index="01"
          kicker="Bausteine"
          title="Bibliotheks-Stack für CFD & Multiphysics"
          lead="Lineare Algebra, GPU-fähige Kinetik und Thermodynamik, Solver-Frameworks, Kopplungs- und Mapping-Schichten sowie Abstraktions- und I/O-Werkzeuge — filterbar nach Kategorie und Status."
        >
          <StackSection />
        </Section>

        <Section
          id="pipeline"
          index="02"
          kicker="Datenpfad"
          title="Von CAD zum adjungierten Design-Loop"
          lead="Acht Stufen, die sich gegenseitig füttern: Der Pilot-Lauf macht den Mesher schlauer, die MeshErrorMap macht die Kernels schneller, und der Adjoint macht beides beim nächsten Mal besser."
        >
          <PipelineSection />
        </Section>

        <Section
          id="errormap"
          index="03"
          kicker="Single Source of Truth"
          title="MeshErrorMap → 4-Segment-Partition"
          lead="Skewness, Nicht-Orthogonalität und Dämpfungsfaktor sind keine Diagnose, sondern ein Datenpfad. Schwellwert verschieben und beobachten, wie sich Speicherlayout und VRAM-Bedarf ändern."
        >
          <MeshErrorMapSection />
        </Section>

        <Section
          id="kernels"
          index="04"
          kicker="Implementierung"
          title="Branchless Kernels & asynchrone Streams"
          lead="Struktur-of-Arrays, zwei getrennte Kernel und drei CUDA-Streams pro Zeitschritt. Die Snippets sind direkt als Diskussionsgrundlage für die Kernel-Signaturen geeignet."
        >
          <div className="space-y-5">
            <CodeSection />
            <KernelTimeline />
          </div>
        </Section>

        <Section
          id="accel"
          index="05"
          kicker="Silizium"
          title="Hardware-Beschleuniger jenseits der FVM-Kerne"
          lead="RT-Cores, Tensor Cores und primal-duale Gitter lösen vier Teilprobleme der Multiphysics nebenher — EM-Strahlung, Wärmestrahlung, Meshing-Abfragen und steife Chemie."
        >
          <AcceleratorSection />
        </Section>

        <Section
          id="scaling"
          index="06"
          kicker="HPC & Cloud"
          title="Multi-Node-Skalierung in der Cloud"
          lead="Hilbert-Partitionierung, Inner/Halo-Overlap und s-Step-Krylov-Löser: Knotenzahl wählen und die Modellkurven für Effizienz, Halo-Volumen und globale Synchronisationen beobachten."
        >
          <ScalingSection />
        </Section>

        <Section
          id="ideen"
          index="07"
          kicker="Roadmap"
          title="Was noch fehlt — 12 weitere Hebel"
          lead="Ergänzend zu Pilot-Meshing, Metrik-Predictor, Shrink-Wrap, MeshErrorMap-Nutzung, EMF/Voronoi-DEC, RT-Cores und Cloud-Skalierung: die nächsten Ansatzpunkte samt Aufwand."
        >
          <IdeasSection />
        </Section>

        <Footer />
      </main>
    </div>
  );
}
