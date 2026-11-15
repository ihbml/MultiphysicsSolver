const LINKS = [
  { href: "#atlas", label: "Atlas" },
  { href: "#stacks", label: "Stacks" },
  { href: "#speed", label: "Speed" },
  { href: "#language", label: "Rust or C++" },
];

export function Header() {
  return (
    <header className="sticky top-0 z-30 border-b border-rule bg-paper/90 backdrop-blur-sm">
      <div className="mx-auto flex h-14 max-w-6xl items-center justify-between gap-4 px-5 sm:px-8">
        <a href="#top" className="flex items-baseline gap-3 rounded-sm">
          <span className="font-mono text-[15px] font-medium tracking-tight">Å→m</span>
          <span className="hidden text-sm text-muted md:inline">
            Multiphysics chemistry solver guide
          </span>
        </a>
        <nav aria-label="Sections">
          <ul className="flex items-center gap-4 text-[13px] sm:gap-6 sm:text-sm">
            {LINKS.map((link) => (
              <li key={link.href}>
                <a
                  href={link.href}
                  className="rounded-sm py-2 text-muted transition-colors hover:text-ink"
                >
                  {link.label}
                </a>
              </li>
            ))}
          </ul>
        </nav>
      </div>
    </header>
  );
}
