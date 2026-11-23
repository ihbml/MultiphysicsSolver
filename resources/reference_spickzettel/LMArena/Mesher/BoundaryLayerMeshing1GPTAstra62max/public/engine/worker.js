/* The same C++20 pipeline is used by the CLI and this dedicated worker. */
importScripts("./hexalab.js");
let engine;
let activeKey = "";
self.onmessage = async ({ data }) => {
  try {
    engine ||= HexaEngine({
      locateFile: (f) => new URL(f, self.location.href).href,
    });
    const module = await engine;
    const key = JSON.stringify([data.geometry, data.params]);
    const { layers, first, growth } = data.params;
    if (data.action === "build" || key !== activeKey) {
      self.postMessage({
        stage: "Checking extrusion, connectivity, and cell quality",
      });
      const result = JSON.parse(
        module.cwrap("build_mesh", "string", [
          "number",
          "number",
          "number",
          "number",
        ])(data.geometry, layers, first, growth),
      );
      if (result.error) throw new Error(result.error);
      activeKey = key;
      if (data.action === "build") {
        self.postMessage({ mesh: result });
        return;
      }
    }
    const fa = JSON.parse(
      module.cwrap("extract_surface", "string", ["number"])(data.patch),
    );
    if (fa.error) throw new Error(fa.error);
    self.postMessage({ fa });
  } catch (e) {
    self.postMessage({ error: e.message || String(e) });
  }
};
