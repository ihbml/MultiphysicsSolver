import { chromium, expect } from "@playwright/test";
import { readFile } from "node:fs/promises";
import assert from "node:assert/strict";

const browser = await chromium.launch({
  headless: true,
  args: ["--no-sandbox"],
});
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));
const button = (name) => page.getByRole("button", { name, exact: true });
const tab = (name) => page.getByRole("tab", { name });
const captureDownload = async (trigger) => {
  const pending = page.waitForEvent("download");
  await trigger();
  const file = await pending;
  return {
    name: file.suggestedFilename(),
    bytes: await readFile(await file.path()),
  };
};

try {
  await page.goto(process.env.BASE_URL || "http://localhost:5173");
  await expect(page.locator(".mesh-numbers")).toContainText("59,694");
  await expect(page.locator("#color-field")).toHaveValue("distance");
  await button("X axis").click();
  await expect(button("X axis")).toHaveAttribute("aria-pressed", "true");
  await page.getByRole("slider", { name: "Slice position" }).press("End");
  await expect(page.locator(".position-value")).toContainText("100.0");
  await button("Flip").click();
  await page.getByRole("switch", { name: "Enable slice plane" }).click();
  await expect(button("Y axis")).toBeDisabled();
  await button("Reset inspection controls").click();
  await page.locator("#color-field").selectOption("type");
  await button("Hexahedra").click();
  await expect(button("Hexahedra")).toHaveAttribute("aria-pressed", "false");
  await button("Hexahedra").click();
  await page.locator("#color-field").selectOption("quality");
  await expect(page.locator(".legend-title")).toHaveText("JENS");
  await page.locator(".view-mode").click();
  await button("Wireframe").click();
  await expect(page.locator(".view-mode")).toContainText("Wireframe");
  await page.locator(".view-mode").click();
  await button("Solid + edges").click();
  await button("Expand viewport").click();
  await expect(page.locator(".viewport-expanded")).toBeVisible();
  await page.keyboard.press("Escape");
  await button("Zoom in").click();
  await button("Fit mesh to view").click();
  await page.locator("#color-field").selectOption("distance");
  await button("Pick cell").click();
  const point = await page.locator("canvas").evaluate((canvas) => {
    const { width: w, height: h } = canvas;
    const pix = canvas.getContext("2d").getImageData(0, 0, w, h).data;
    for (let y = 30; y < h - 30; y++)
      for (let x = 60; x < w - 60; x++) {
        const k = 4 * (y * w + x);
        if (pix[k] > 192 && pix[k + 1] > 140 && pix[k + 2] < 155)
          return {
            x: (x * canvas.clientWidth) / w,
            y: (y * canvas.clientHeight) / h,
          };
      }
    throw Error("No colored section polygon found");
  });
  await page.locator("canvas").click({ position: point });
  await expect(tab("Cell inspector")).toHaveAttribute("aria-selected", "true");
  await page.locator("#cell-id").fill("0");
  await button("Inspect").click();
  await expect(page.locator(".detail-data")).toContainText("#0");
  await page.locator("#cell-id").fill("999999");
  await button("Inspect").click();
  await expect(page.locator(".error-text")).toContainText("Enter an ID");
  console.log(
    "PASS: clipping, scalar fields, visibility, camera, wireframe, fullscreen, and cell picking",
  );

  await button("Run verification").click();
  await expect(page.getByRole("status")).toContainText(
    "Verification complete",
    { timeout: 60000 },
  );
  await tab("Quality overview").click();
  await button("All checks passed").click();
  await expect(page.getByRole("dialog")).toContainText(
    "Hanging vertices on mesh edges",
  );
  await page.keyboard.press("Escape");
  await button("Export mesh or report").click();
  const reportFile = await captureDownload(() =>
    page.getByRole("button", { name: "Verification report JSON" }).click(),
  );
  const report = JSON.parse(reportFile.bytes);
  assert.equal(report.results.cells, 59694);
  assert.equal(report.results.hangingNodes, 0);
  assert.equal(report.results.coarseParents, 76);
  assert.ok(report.results.minJ > 0.25 && report.results.maxClosure < 1e-14);
  await button("Export mesh or report").click();
  const volumeFile = await captureDownload(() =>
    page.getByRole("button", { name: "Volume mesh VTK" }).click(),
  );
  assert.match(volumeFile.bytes.toString(), /DATASET UNSTRUCTURED_GRID/);
  assert.match(volumeFile.bytes.toString(), /CELL_TYPES 59694/);
  console.log(
    "PASS: compiled WASM verification, report criteria, JSON report and VTK export",
  );

  await button("Documentation").click();
  await button("Build locally").click();
  const source = await captureDownload(() =>
    page.getByRole("link", { name: "Download C++20 source" }).click(),
  );
  assert.equal(source.bytes.readUInt32LE(0), 0x04034b50);
  await page.keyboard.press("Escape");
  await button("Mesh library").click();
  await page.locator(".geometry-option").nth(1).click();
  await expect(page.locator(".mesh-numbers")).toContainText("32,504");
  await page.getByRole("button", { name: /Meshing parameters/ }).click();
  await page.getByLabel(/Prism layers/).fill("4");
  await page.getByLabel(/First-layer height/).fill("0.15");
  await button("Rebuild mesh").click();
  await expect(page.getByRole("status")).toContainText("Mesh rebuilt", {
    timeout: 60000,
  });
  await expect(page.locator(".mesh-numbers")).toContainText("25,592");
  await expect(page.locator(".parameters-link")).toContainText("4 layers");
  console.log(
    "PASS: geometry library, documentation, source download, and parameterized C++ rebuild",
  );

  await tab("Wall submesh").click();
  await page.getByLabel("Wall patch", { exact: true }).selectOption("0");
  await button("Preview").click();
  await expect(page.locator(".scene-bottom")).toContainText("Surface view");
  await button("Extract surface").click();
  await expect(page.locator(".surface-data")).toContainText("1,280 linked", {
    timeout: 60000,
  });
  const wallFile = await captureDownload(() => button("faMesh").click());
  const wall = JSON.parse(wallFile.bytes);
  assert.equal(wall.faces.length, 1280);
  assert.ok(wall.uniqueOwners && wall.boundaryEdges > 0);
  assert.equal(
    new Set(wall.faces.map((f) => f.face_to_volume_owner_cell)).size,
    1280,
  );
  assert.equal(
    wall.faces.reduce((s, f) => s + f.area, 0),
    2560,
  );
  assert.ok(wall.faces.every((f) => f.curvature.every((x) => x === 0)));
  await page.getByLabel("Wall patch", { exact: true }).selectOption("-1");
  await button("Extract surface").click();
  await expect(page.locator(".surface-data")).toContainText("3,456 linked", {
    timeout: 60000,
  });
  const closed = JSON.parse(
    (await captureDownload(() => button("faMesh").click())).bytes,
  );
  assert.equal(closed.boundaryEdges, 0);
  assert.ok(
    closed.faces.every((f) =>
      f.edges.every((e) => e.neighbor >= 0 && e.geodesic > 0),
    ),
  );
  await tab("Engine log").click();
  await expect(page.locator(".engine-log")).toContainText("Extracted 3,456");
  const logfile = await captureDownload(() => button("Save log").click());
  assert.match(logfile.bytes.toString(), /PASS/);
  console.log(
    "PASS: patch extraction, closed shell, unique owners, areas, curvature, geodesics, and logs",
  );

  for (const width of [360, 390, 768, 1024, 1440]) {
    await page.setViewportSize({ width, height: 840 });
    assert.ok(
      await page.evaluate(
        () => document.documentElement.scrollWidth <= innerWidth,
      ),
      `Horizontal overflow at ${width}`,
    );
  }
  assert.deepEqual(errors, []);
  console.log(
    "PASS: responsive layout at five widths and no browser runtime errors",
  );
} finally {
  await browser.close();
}
