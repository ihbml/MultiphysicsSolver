import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
// Barlow (OFL) – DIN-naher Duktus für Oberfläche und Überschriften, nur Latin-Subset
import "@fontsource/barlow/latin-400.css";
import "@fontsource/barlow/latin-500.css";
import "@fontsource/barlow/latin-600.css";
import "@fontsource/barlow-condensed/latin-500.css";
import "@fontsource/barlow-condensed/latin-600.css";
import "./index.css";
import App from "./App";

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>
);
