import type { Metadata } from "next";
import type { ReactNode } from "react";
import "./globals.css";

export const metadata: Metadata = {
  title: "FVM / SPH Mesher — native solver backend",
  description:
    "Voronoi finite-volume mesher for SPH particle clouds with Eulerian-SPH consistent boundaries, HLLC graph solver, particle-mesh interpolation and design sensitivities.",
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body className="min-h-screen antialiased">{children}</body>
    </html>
  );
}
