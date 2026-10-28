"""Integration checks: browser API -> real C++ CLI -> CSV preview and ZIP."""

from __future__ import annotations

from http.client import HTTPConnection
import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
import zipfile

from server import AppServer, DEFAULT_BINARY, ROOT


class WebIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not DEFAULT_BINARY.exists():
            raise RuntimeError("build the C++ engine first: make -j4 all")
        cls.temporary = tempfile.TemporaryDirectory(prefix="hybridmesh-web-test-")
        cls.server = AppServer(("127.0.0.1", 0),
                               runs=Path(cls.temporary.name), binary=DEFAULT_BINARY)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.thread.join(timeout=4)
        cls.server.server_close()
        cls.temporary.cleanup()

    def request(self, method: str, path: str, payload: dict | None = None):
        conn = HTTPConnection("127.0.0.1", self.server.server_port, timeout=15)
        if payload is None:
            conn.request(method, path)
        else:
            conn.request(method, path, body=json.dumps(payload),
                         headers={"Content-Type": "application/json"})
        response = conn.getresponse()
        status, data = response.status, response.read()
        conn.close()
        return status, data

    def test_2d_default_and_download(self) -> None:
        status, data = self.request("POST", "/api/run", {
            "source": {"type": "box2", "nx": 16, "ny": 16, "lx": 1, "ly": 1},
            "core": {"type": "box", "min": [.25, .25], "max": [.75, .75]},
            "spacing": 1 / 16, "support": 2.6, "band": 2, "include_links": True,
        })
        self.assertEqual(status, 200, data)
        result = json.loads(data)
        self.assertEqual(result["summary"]["cells"], 256)
        self.assertEqual(result["preview"]["dimension"], 2)
        self.assertGreater(result["summary"]["sph_directed_links"], 0)
        self.assertTrue(result["preview"]["nodes"])
        self.assertIn("fvm_graph.csv", result["files"])
        key = result["job_id"]
        status, text = self.request("GET", f"/api/jobs/{key}/file/summary.txt")
        self.assertEqual(status, 200)
        self.assertIn(b"common_particles", text)
        status, archive = self.request("GET", f"/api/jobs/{key}/download")
        self.assertEqual(status, 200)
        with zipfile.ZipFile(io.BytesIO(archive)) as z:
            self.assertIn("mesh.vtk", z.namelist())
            self.assertIn("mesh_to_particle.csv", z.namelist())
        self.assertEqual(self.request("GET", f"/api/jobs/{key}/file/../../README.md")[0], 404)

    def test_3d_slice_data(self) -> None:
        status, data = self.request("POST", "/api/run", {
            "source": {"type": "box3", "nx": 8, "ny": 8, "nz": 8,
                       "lx": 1, "ly": 1, "lz": 1},
            "core": {"type": "ball", "center": [.5, .5, .5], "radius": .2},
            "spacing": .125,
        })
        self.assertEqual(status, 200, data)
        result = json.loads(data)
        self.assertEqual(result["summary"]["cells"], 512)
        self.assertEqual(result["preview"]["dimension"], 3)
        self.assertEqual(result["preview"]["nodes"], {})
        self.assertNotIn("sph_neighbors.csv", result["files"])
        self.assertTrue(any(c["role"] == "common" for c in result["preview"]["cells"]))

    def test_uploaded_gmsh_and_poly_mesh(self) -> None:
        gmsh = (ROOT / "tests/fixtures/square.msh").read_text()
        status, data = self.request("POST", "/api/run", {
            "source": {"type": "gmsh_upload", "text": gmsh},
            "core": {"type": "all"}, "spacing": .15,
        })
        self.assertEqual(status, 200, data)
        self.assertEqual(json.loads(data)["summary"]["cells"], 2)
        folder = ROOT / "tests/fixtures/poly_prism"
        files = {name: (folder / name).read_text() for name in
                 ("points", "faces", "owner", "neighbour", "boundary")}
        status, data = self.request("POST", "/api/run", {
            "source": {"type": "foam_upload", "files": files},
            "core": {"type": "all"}, "spacing": .4,
        })
        self.assertEqual(status, 200, data)
        result = json.loads(data)
        self.assertEqual(result["summary"]["dimension"], 3)
        self.assertEqual(result["summary"]["cells"], 1)
        self.assertNotIn("mesh.vtk", result["files"])  # arbitrary polyhedron
        self.assertIn("particles.vtk", result["files"])

    def test_physical_wall_and_adaptive_step(self) -> None:
        status, data = self.request("POST", "/api/run", {
            "source": {"type": "box2", "nx": 20, "ny": 20, "lx": 1, "ly": 1},
            "core": {"type": "all"}, "spacing": .05,
        })
        self.assertEqual(status, 200, data)
        full = json.loads(data)
        self.assertGreater(full["summary"]["physical_boundary_particles"], 0)
        self.assertTrue(any(p["boundary"] for p in full["preview"]["particles"]))
        status, data = self.request("POST", "/api/run", {
            "source": {"type": "box2", "nx": 20, "ny": 20, "lx": 1, "ly": 1},
            "core": {"type": "box", "min": [.25, .25], "max": [.5, .5]},
            "spacing": .05,
            "adapt": {"csv": "x,y\n0.7,0.7\n0.75,0.75\n", "margin": .08, "step": .05},
        })
        self.assertEqual(status, 200, data)
        self.assertIn("Adaptive box moved: yes", json.loads(data)["log"])

    def test_reject_bad_inputs_and_serve_assets(self) -> None:
        status, body = self.request("GET", "/api/health")
        self.assertEqual(status, 200)
        self.assertTrue(json.loads(body)["ready"])
        for page, content in (("/", b"HybridMesh"), ("/app.js", b"/api/run"),
                              ("/style.css", b".layout")):
            status, body = self.request("GET", page)
            self.assertEqual(status, 200)
            self.assertIn(content, body)
        status, body = self.request("POST", "/api/run", {
            "source": {"type": "box2", "nx": 20, "ny": 20, "lx": 1, "ly": 1},
            "core": {"type": "ball", "center": [.5, .5], "radius": .2},
            "spacing": 0.000001,
        })
        self.assertEqual(status, 422)
        self.assertIn("too fine", json.loads(body)["error"])
        status, body = self.request("POST", "/api/run", {
            "source": {"type": "box2", "nx": 10, "ny": 10, "lx": 1, "ly": 1},
            "core": {"type": "box", "min": [.8, .8], "max": [.2, .9]},
            "spacing": .1,
        })
        self.assertEqual(status, 422)
        self.assertIn("maximum", json.loads(body)["error"])


if __name__ == "__main__":
    unittest.main()
