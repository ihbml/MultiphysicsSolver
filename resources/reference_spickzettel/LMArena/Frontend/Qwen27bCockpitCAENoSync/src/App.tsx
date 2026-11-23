import { useEffect } from "react";
import { TopBar } from "./components/TopBar";
import { BlockStack } from "./components/BlockStack";
import { Viewport } from "./components/Viewport";
import { Dock, ConsoleStrip } from "./components/Dock";
import { Inspector } from "./components/Inspector";
import { bootBackend } from "./ipc/backend";

export default function App() {
  useEffect(() => {
    bootBackend().catch((e) => console.error("boot failed", e));
  }, []);

  return (
    <div className="h-full flex flex-col bg-[#111217] text-[#E8E9ED] overflow-hidden">
      <TopBar />
      <div className="flex-1 flex min-h-0">
        <BlockStack />
        <Viewport />
        <Inspector />
      </div>
      <Dock />
      <ConsoleStrip />
    </div>
  );
}
