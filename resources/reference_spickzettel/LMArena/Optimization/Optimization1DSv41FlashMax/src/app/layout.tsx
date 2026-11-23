import type { Metadata } from "next";
import type { ReactNode } from "react";
import "./globals.css";

export const metadata: Metadata = {
  title: "MDO Phase 4 · Optimisation Cockpit",
  description:
    "Thermo-fluid topology optimisation, Svanberg MMA, Revolve checkpointing, FOSM robust design and hybrid shape search.",
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body className="min-h-screen bg-[#05080e] font-sans text-slate-200 antialiased selection:bg-cyan-400/30">{children}</body>
    </html>
  );
}
