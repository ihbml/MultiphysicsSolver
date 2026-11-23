import { chromium } from "@playwright/test";
import { mkdir } from "node:fs/promises";

await mkdir("artifacts", { recursive: true });
const baseURL = process.env.BASE_URL || "http://localhost:5173";
const browser = await chromium.launch({ headless: true });
const page = await browser.newPage({
  viewport: { width: 1440, height: 1050 },
  deviceScaleFactor: 1,
});
const errors = [];
page.on("pageerror", (error) => errors.push(error.message));
await page.goto(baseURL, { waitUntil: "networkidle" });
await page.waitForTimeout(700);
await page.screenshot({
  path: "artifacts/workbench-desktop.png",
  fullPage: true,
});
console.log(
  "Initial metrics:",
  await page.locator(".metric-value").allTextContents(),
);
await page
  .getByRole("button", { name: "Resume simulation", exact: true })
  .click();
await page.waitForTimeout(350);
await page
  .getByRole("button", { name: "Pause simulation", exact: true })
  .click();
console.log("Playback:", await page.locator(".timeline-time").textContent());
await page.getByLabel("Coolant temperature", { exact: true }).fill("300");
await page.getByLabel("Coolant temperature", { exact: true }).press("Enter");
if (
  (await page
    .getByLabel("Coolant temperature", { exact: true })
    .inputValue()) !== "300"
)
  throw new Error("Numeric parameter was not committed.");
await page
  .getByLabel("Visualized field", { exact: true })
  .selectOption("Reaction progress");
await page
  .getByLabel("Visualized field", { exact: true })
  .selectOption("Temperature");
await page.getByRole("button", { name: "Section", exact: true }).click();
await page.locator(".battery-svg .cell-shape").first().click();
console.log(
  "Selected cell:",
  await page.locator(".field-selection").textContent(),
);
await page
  .getByRole("button", { name: "Save run snapshot", exact: true })
  .click();
await page
  .getByLabel("RUN NAME", { exact: true })
  .fill("Playwright reference run");
await page
  .getByRole("dialog")
  .getByRole("button", { name: "Save run snapshot", exact: true })
  .click();
await page
  .locator(".main-nav")
  .getByRole("button", { name: /Simulation runs/ })
  .click();
await page.getByText("Playwright reference run", { exact: true }).waitFor();
await page
  .getByRole("button", { name: "Restore", exact: false })
  .first()
  .click();
await page.getByRole("tab", { name: /Aeroacoustics/ }).click();
await page.getByRole("heading", { name: "Acoustic spectrum" }).waitFor();
console.log(
  "FWH metrics:",
  await page.locator(".metric-value").allTextContents(),
);
await page.getByRole("button", { name: "1/3 octave", exact: true }).click();
await page
  .getByRole("button", { name: "Select microphone 2", exact: true })
  .click();
await page.getByRole("button", { name: "Export data", exact: false }).click();
const [csv] = await Promise.all([
  page.waitForEvent("download"),
  page.getByRole("button", { name: /Telemetry CSV/ }).click(),
]);
console.log("Export:", csv.suggestedFilename());
await page.getByRole("tab", { name: /Magnetohydrodynamics/ }).click();
await page
  .getByRole("heading", { name: "Velocity profile validation" })
  .waitFor();
await page.getByLabel("Hartmann number", { exact: true }).focus();
await page.keyboard.press("End");
console.log(
  "MHD metrics:",
  await page.locator(".metric-value").allTextContents(),
);
await page.getByRole("tab", { name: /Ablation & TPS/ }).click();
await page
  .getByRole("heading", { name: "Through-thickness response" })
  .waitFor();
console.log(
  "TPS metrics:",
  await page.locator(".metric-value").allTextContents(),
);
await page.getByRole("button", { name: "Density", exact: true }).click();
await page.getByRole("button", { name: "Temperature", exact: true }).click();
if (await page.locator(".toast").count())
  await page
    .getByRole("button", { name: "Dismiss notification", exact: true })
    .click();
await page.waitForTimeout(700);
await page.evaluate(() => window.scrollTo(0, 0));
await page.screenshot({ path: "artifacts/workbench-tps.png", fullPage: true });
await page
  .locator(".main-nav")
  .getByRole("button", { name: "Verification", exact: true })
  .click();
const native = await (
  await page.request.get(`${baseURL}/verification.json`)
).json();
if (!native.tests.every((test) => test.passed))
  throw new Error("Native verification report contains failures.");
await page
  .getByText(
    `${native.tests.length} / ${native.tests.length} native checks passed`,
    { exact: true },
  )
  .waitFor();
console.log("Native report:", native.tests.length, "passed");
await page
  .getByRole("button", { name: "Run browser tests", exact: true })
  .click();
await page.getByText("8 / 8 browser checks passed", { exact: true }).waitFor();
console.log(
  "Browser verification:",
  await page.locator(".pass-status").count(),
  "passed",
);
await page.screenshot({ path: "artifacts/verification.png", fullPage: true });
await page
  .locator(".main-nav")
  .getByRole("button", { name: "Source & API", exact: true })
  .click();
await page.getByRole("button", { name: "C++ source", exact: true }).click();
await page
  .locator(".code-view")
  .getByText("cluster_c.cpp", { exact: true })
  .waitFor();
await page
  .locator(".main-nav")
  .getByRole("button", { name: "Mesh explorer", exact: true })
  .click();
await page.getByRole("button", { name: "Face topology", exact: true }).click();
await page
  .getByRole("button", { name: "Cell 2", exact: false })
  .first()
  .click();
console.log(
  "Mesh inspector:",
  await page.locator(".mesh-layout .panel").last().locator("h2").textContent(),
);
await page
  .locator(".main-nav")
  .getByRole("button", { name: "Workbench", exact: true })
  .click();
await page.getByRole("tab", { name: /Battery thermal/ }).click();
const mobile = await browser.newPage({
  viewport: { width: 390, height: 844 },
  deviceScaleFactor: 1,
});
mobile.on("pageerror", (error) => errors.push(error.message));
await mobile.goto(baseURL, { waitUntil: "networkidle" });
await mobile.waitForTimeout(800);
await mobile.screenshot({
  path: "artifacts/workbench-mobile.png",
  fullPage: true,
});
const overflow = await mobile.evaluate(
  () => document.documentElement.scrollWidth > window.innerWidth,
);
if (overflow) throw new Error("Mobile viewport has horizontal overflow.");
await mobile
  .getByRole("button", { name: "Open navigation", exact: true })
  .click();
await mobile
  .locator(".main-nav")
  .getByRole("button", { name: "Verification", exact: true })
  .click();
await mobile
  .getByRole("heading", { name: "Verification", exact: true })
  .waitFor();
await browser.close();
if (errors.length) throw new Error(errors.join("\n"));
console.log(
  "All interaction checks passed; no browser exceptions or mobile overflow.",
);
