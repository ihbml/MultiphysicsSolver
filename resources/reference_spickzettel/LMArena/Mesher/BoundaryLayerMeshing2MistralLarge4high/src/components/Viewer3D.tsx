import { useEffect, useRef } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import type { MeshData } from "@/lib/types";
import { buildCellGeometry, buildFaGeometry, buildSliceLines, bbox, type ColorMode, type Slices } from "@/lib/mesh";

interface Props {
  data: MeshData;
  colorMode: ColorMode;
  slices: Slices;
  showFa: boolean;
  highlightCell: number | null;
  onPick: (cell: number | null) => void;
}

export default function Viewer3D({ data, colorMode, slices, showFa, highlightCell, onPick }: Props) {
  const mountRef = useRef<HTMLDivElement>(null);
  const stateRef = useRef<{
    renderer: THREE.WebGLRenderer;
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera;
    controls: OrbitControls;
    mesh: THREE.Mesh;
    edges: THREE.LineSegments;
    fa: THREE.Mesh | null;
    sliceLines: THREE.LineSegments[];
    highlight: THREE.LineSegments;
    raycaster: THREE.Raycaster;
    cellMesh: THREE.Mesh;
  } | null>(null);

  // (re)build scene contents when dataset changes
  useEffect(() => {
    const mount = mountRef.current!;
    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.localClippingEnabled = true;
    mount.appendChild(renderer.domElement);
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0b1220);
    const camera = new THREE.PerspectiveCamera(45, 1, 1e-6, 1e4);
    const controls = new OrbitControls(camera, renderer.domElement);
    scene.add(new THREE.AmbientLight(0xffffff, 0.9));
    const dl = new THREE.DirectionalLight(0xffffff, 1.2);
    dl.position.set(1, 2, 3);
    scene.add(dl);
    const dl2 = new THREE.DirectionalLight(0xbfdbfe, 0.5);
    dl2.position.set(-2, -1, -2);
    scene.add(dl2);

    const built = buildCellGeometry(data, colorMode);
    const mat = new THREE.MeshStandardMaterial({
      vertexColors: true,
      transparent: true,
      opacity: 0.82,
      roughness: 0.55,
      metalness: 0.05,
      side: THREE.DoubleSide,
      depthWrite: false,
    });
    const mesh = new THREE.Mesh(built.geometry, mat);
    scene.add(mesh);
    const edges = new THREE.LineSegments(
      built.edges,
      new THREE.LineBasicMaterial({ color: 0x1e293b, transparent: true, opacity: 0.35 })
    );
    scene.add(edges);
    const faGeo = buildFaGeometry(data);
    const fa = faGeo
      ? new THREE.Mesh(
          faGeo,
          new THREE.MeshStandardMaterial({ vertexColors: true, side: THREE.DoubleSide, roughness: 0.4 })
        )
      : null;
    if (fa) scene.add(fa);
    const highlight = new THREE.LineSegments(
      new THREE.BufferGeometry(),
      new THREE.LineBasicMaterial({ color: 0xfef08a, linewidth: 2 })
    );
    scene.add(highlight);

    const bb = bbox(data);
    const center = bb.min.clone().add(bb.max).multiplyScalar(0.5);
    const size = bb.max.clone().sub(bb.min).length();
    camera.position.set(center.x + size * 0.7, center.y + size * 0.5, center.z + size * 0.9);
    controls.target.copy(center);
    controls.update();

    const raycaster = new THREE.Raycaster();
    stateRef.current = { renderer, scene, camera, controls, mesh, edges, fa, sliceLines: [], highlight, raycaster, cellMesh: mesh };

    const onClick = (ev: MouseEvent) => {
      const rect = renderer.domElement.getBoundingClientRect();
      const ndc = new THREE.Vector2(
        ((ev.clientX - rect.left) / rect.width) * 2 - 1,
        -((ev.clientY - rect.top) / rect.height) * 2 + 1
      );
      raycaster.setFromCamera(ndc, camera);
      const hits = raycaster.intersectObject(mesh);
      if (hits.length > 0) {
        const tri = hits[0].faceIndex!;
        onPick(built.cellOfTri[tri] ?? null);
      } else onPick(null);
    };
    renderer.domElement.addEventListener("click", onClick);

    const onResize = () => {
      const w = mount.clientWidth;
      const h = mount.clientHeight;
      renderer.setSize(w, h);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
    };
    onResize();
    window.addEventListener("resize", onResize);

    let raf = 0;
    const loop = () => {
      raf = requestAnimationFrame(loop);
      controls.update();
      renderer.render(scene, camera);
    };
    loop();

    return () => {
      cancelAnimationFrame(raf);
      window.removeEventListener("resize", onResize);
      renderer.domElement.removeEventListener("click", onClick);
      controls.dispose();
      renderer.dispose();
      mount.removeChild(renderer.domElement);
      stateRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [data]);

  // color mode: rebuild geometry colors
  useEffect(() => {
    const st = stateRef.current;
    if (!st) return;
    const built = buildCellGeometry(data, colorMode);
    st.mesh.geometry.dispose();
    st.mesh.geometry = built.geometry;
    st.edges.geometry.dispose();
    st.edges.geometry = built.edges;
  }, [data, colorMode]);

  // clipping planes + slice cross-section lines
  useEffect(() => {
    const st = stateRef.current;
    if (!st) return;
    const planes: THREE.Plane[] = [];
    const ax: [number, "x" | "y" | "z"][] = [
      [0, "x"],
      [1, "y"],
      [2, "z"],
    ];
    for (const [a, key] of ax) {
      const s = slices[key];
      if (s.on) planes.push(new THREE.Plane(new THREE.Vector3().setComponent(a, -1), s.val));
    }
    (st.mesh.material as THREE.MeshStandardMaterial).clippingPlanes = planes;
    (st.edges.material as THREE.LineBasicMaterial).clippingPlanes = planes;
    // rebuild slice lines
    for (const l of st.sliceLines) {
      st.scene.remove(l);
      l.geometry.dispose();
    }
    st.sliceLines = [];
    for (const [, key] of ax) {
      const s = slices[key];
      if (!s.on) continue;
      const g = buildSliceLines(data, key, s.val);
      const l = new THREE.LineSegments(
        g,
        new THREE.LineBasicMaterial({ color: key === "x" ? 0x22d3ee : key === "y" ? 0xf472b6 : 0xfacc15 })
      );
      st.scene.add(l);
      st.sliceLines.push(l);
    }
  }, [data, slices]);

  // faMesh visibility
  useEffect(() => {
    const st = stateRef.current;
    if (st?.fa) st.fa.visible = showFa;
  }, [showFa, data]);

  // highlight picked cell
  useEffect(() => {
    const st = stateRef.current;
    if (!st) return;
    if (highlightCell == null) {
      st.highlight.geometry.dispose();
      st.highlight.geometry = new THREE.BufferGeometry();
      return;
    }
    const cell = data.cells[highlightCell];
    const V = data.vertices;
    const pos: number[] = [];
    const faces =
      cell.t === 0
        ? [
            [0, 3, 2, 1],
            [4, 5, 6, 7],
            [0, 1, 5, 4],
            [1, 2, 6, 5],
            [2, 3, 7, 6],
            [3, 0, 4, 7],
          ]
        : [
            [0, 2, 1],
            [3, 4, 5],
            [0, 1, 4, 3],
            [1, 2, 5, 4],
            [2, 0, 3, 5],
          ];
    for (const f of faces)
      for (let i = 0; i < f.length; i++) {
        const a = V[cell.n[f[i]]];
        const b = V[cell.n[f[(i + 1) % f.length]]];
        pos.push(a[0], a[1], a[2], b[0], b[1], b[2]);
      }
    st.highlight.geometry.dispose();
    st.highlight.geometry = new THREE.BufferGeometry();
    st.highlight.geometry.setAttribute("position", new THREE.Float32BufferAttribute(pos, 3));
  }, [highlightCell, data]);

  return <div ref={mountRef} className="h-full w-full" />;
}
