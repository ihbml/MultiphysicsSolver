import { Atlas } from "./components/Atlas";
import { Footer } from "./components/Footer";
import { Header } from "./components/Header";
import { Hero } from "./components/Hero";
import { Language } from "./components/Language";
import { Speed } from "./components/Speed";
import { Stacks } from "./components/Stacks";

export default function App() {
  return (
    <div className="min-h-screen overflow-x-clip">
      <a
        href="#atlas"
        className="sr-only focus:not-sr-only focus:fixed focus:top-2 focus:left-2 focus:z-50 focus:rounded-sm focus:bg-ink focus:px-3 focus:py-2 focus:text-paper"
      >
        Skip to the solver atlas
      </a>
      <Header />
      <main>
        <Hero />
        <Atlas />
        <Stacks />
        <Speed />
        <Language />
      </main>
      <Footer />
    </div>
  );
}
