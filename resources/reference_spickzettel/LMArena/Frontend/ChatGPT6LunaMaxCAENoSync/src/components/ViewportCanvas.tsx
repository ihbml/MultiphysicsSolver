import { forwardRef, useEffect, useImperativeHandle, useRef, useState } from "react";
import { CfdViewportRenderer, type ViewportOptions } from "@/lib/viewportRenderer";

export type ViewportCanvasHandle = { resetCamera: () => void };

type ViewportCanvasProps = {
  options: ViewportOptions;
  onPatchPick: (patch: string) => void;
};

const ViewportCanvas = forwardRef<ViewportCanvasHandle, ViewportCanvasProps>(function ViewportCanvas({ options, onPatchPick }, ref) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const rendererRef = useRef<CfdViewportRenderer | null>(null);
  const pickRef = useRef(onPatchPick);
  const [error, setError] = useState("");

  useEffect(() => { pickRef.current = onPatchPick; }, [onPatchPick]);

  useImperativeHandle(ref, () => ({ resetCamera: () => rendererRef.current?.resetCamera() }), []);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    try {
      const renderer = new CfdViewportRenderer(canvas, (patch) => pickRef.current(patch), options);
      rendererRef.current = renderer;
      return () => {
        renderer.dispose();
        rendererRef.current = null;
      };
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "Could not initialize WebGL2");
    }
  }, []);

  useEffect(() => { rendererRef.current?.update(options); }, [options]);

  return (
    <div className="viewport-surface">
      <canvas ref={canvasRef} className="viewport-canvas" aria-label="Interactive 3D CFD scene. Drag to orbit, scroll to zoom, click to select a patch." />
      {error && <div className="webgl-error">{error}. The rest of the cockpit remains available.</div>}
    </div>
  );
});

export default ViewportCanvas;