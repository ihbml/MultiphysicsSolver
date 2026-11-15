export function Footer() {
  const links = [
    { label: "ginkgo", href: "https://github.com/ginkgo-project/ginkgo" },
    { label: "SU2", href: "https://github.com/su2code/SU2" },
    { label: "preCICE", href: "https://github.com/precice/precice" },
    { label: "Kokkos", href: "https://github.com/kokkos/kokkos" },
    { label: "Enzyme", href: "https://github.com/EnzymeAD/Enzyme" },
    { label: "AMReX", href: "https://github.com/AMReX-Codes/amrex" },
    { label: "MFEM", href: "https://github.com/mfem/mfem" },
    { label: "OpenVDB", href: "https://github.com/AcademySoftwareFoundation/openvdb" },
  ];

  return (
    <footer className="border-t border-white/10 px-5 py-12 sm:px-8 lg:px-14">
      <div className="mx-auto max-w-6xl">
        <div className="grid gap-8 md:grid-cols-[1.4fr_1fr]">
          <div>
            <div className="mb-2 font-mono text-xs font-semibold tracking-[0.18em] text-white">
              MESHFORGE · ARCHITECTURE HUB
            </div>
            <p className="max-w-xl text-[12.5px] leading-relaxed text-slate-500">
              Konzept- und Dokumentationsoberfläche für einen GPU-beschleunigten CFD- und
              Multiphysics-Solver. Alle Kennzahlen (65 % VRAM-Ersparnis, Segmentanteile,
              Skalierungskurven) sind Modellrechnungen aus der Face-Partition bzw. dem
              angegebenen Strong-Scaling-Modell — keine Benchmarks. Code-Snippets sind
              illustrativ und nicht kompiliert.
            </p>
          </div>
          <div>
            <div className="mb-3 font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
              Referenz-Repositories
            </div>
            <div className="flex flex-wrap gap-x-4 gap-y-2">
              {links.map((l) => (
                <a
                  key={l.label}
                  href={l.href}
                  target="_blank"
                  rel="noreferrer noopener"
                  className="font-mono text-[11.5px] text-slate-400 transition-colors hover:text-cyan-300"
                >
                  {l.label} ↗
                </a>
              ))}
            </div>
          </div>
        </div>
        <div className="mt-8 border-t border-white/5 pt-6 text-[11px] text-slate-600">
          Gebaut mit React · Vite · Tailwind · SVG — keine externen Assets, keine Tracker.
        </div>
      </div>
    </footer>
  );
}
