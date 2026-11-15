import { expect, test } from '@playwright/test';
import { readFile } from 'node:fs/promises';

test('overview renders without runtime errors or horizontal overflow', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'Good physics. Better engineering.' })).toBeVisible();
  await expect(page.getByRole('img', { name: /triangular torus/ })).toBeVisible();
  await expect(page.locator('.featured-note')).toHaveCount(3);
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  expect(errors).toEqual([]);
});

test('review filters critical corrections and expands the explanation', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Explore the review' }).click();
  await page.getByRole('button', { name: 'Kritisch 3' }).click();
  await expect(page.locator('.correction-item')).toHaveCount(3);
  await expect(page.getByText('THE CORRECTION', { exact: true })).toBeVisible();
  await page.getByRole('button', { name: /Ein Feld-Halo liefert/ }).click();
  await expect(page.getByText(/Entweder Gradienten austauschen/)).toBeVisible();
});

test('keyboard search opens a note and closes with escape', async ({ page }) => {
  await page.goto('/');
  await page.keyboard.press('Control+k');
  const input = page.getByRole('textbox', { name: 'Search notes, code and libraries' });
  await expect(input).toBeFocused();
  await input.fill('Der Halo');
  await input.press('Enter');
  await expect(page.locator('.article-document h1')).toHaveText('Der Halo ist ein Datenvertrag.');
  await page.keyboard.press('Control+k');
  await page.keyboard.press('Escape');
  await expect(page.getByRole('dialog')).toHaveCount(0);
});

test('snippet filters persist during selection and files download', async ({ page }) => {
  await page.goto('/#snippets');
  await page.getByRole('button', { name: 'CUDA', exact: true }).click();
  await expect(page.locator('.snippet-list-item')).toHaveCount(4);
  await page.locator('.snippet-list-item').filter({ hasText: 'correction_flux.cu' }).click();
  await expect(page.locator('.snippet-list-item')).toHaveCount(4);
  await expect(page.locator('.snippet-detail h2')).toHaveText('Sparse Geometriekorrektur');
  const event = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Download correction_flux.cu' }).click();
  const download = await event;
  expect(download.suggestedFilename()).toBe('correction_flux.cu');
  const contents = await readFile((await download.path())!, 'utf8');
  expect(contents).toContain('__global__ void correction_flux');
});

test('code can be copied with visible confirmation', async ({ page, context }) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  await page.goto('/#snippets/layout');
  await page.getByRole('button', { name: 'Copy face_layout.hpp' }).click();
  await expect(page.getByRole('button', { name: 'Copy face_layout.hpp' })).toHaveText('Copied');
  expect(await page.evaluate(() => navigator.clipboard.readText())).toContain('struct CorrectionSoA');
});

test('repository filters expose useful integration warnings', async ({ page }) => {
  await page.goto('/#libraries');
  await page.getByRole('button', { name: 'Kinetik', exact: true }).click();
  await expect(page.locator('.library-row')).toHaveCount(4);
  await page.getByRole('textbox', { name: 'Search libraries' }).fill('Pele');
  await expect(page.locator('.library-row')).toHaveCount(1);
  await page.getByRole('button', { name: 'Integration notes for PelePhysics' }).click();
  await expect(page.locator('.repository-caution')).toContainText('Mechanismus- und Backend');
  await expect(page.getByRole('link', { name: 'PelePhysics' })).toHaveAttribute('href', 'https://github.com/Pele-Suite/PelePhysics');
});

test('bookmarks are saved locally across reloads', async ({ page }) => {
  await page.goto('/#note/mesh-quality');
  await page.getByRole('button', { name: 'Save note', exact: true }).click();
  await page.goto('/#saved');
  await expect(page.locator('.article-list-row')).toHaveCount(1);
  await page.reload();
  await expect(page.locator('.article-list-row')).toHaveCount(1);
  await page.getByRole('button', { name: /^Unsave/ }).click();
  await expect(page.getByRole('heading', { name: 'A little room for good ideas.' })).toBeVisible();
});

test('implementation progress persists and architecture can be exported', async ({ page }) => {
  await page.goto('/#architecture');
  await page.getByRole('button', { name: 'Mark complete: Die numerische Referenz' }).click();
  await page.reload();
  await expect(page.locator('.roadmap-step.complete')).toHaveCount(1);
  const event = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Export architecture' }).click();
  const download = await event;
  expect(download.suggestedFilename()).toBe('ARCHITECTURE.md');
  const contents = await readFile((await download.path())!, 'utf8');
  expect(contents).toContain('- [x] 01.');
  await page.getByRole('button', { name: 'Reset progress' }).click();
  await expect(page.locator('.roadmap-step.complete')).toHaveCount(0);
});

test('interactive mesh responds to threshold changes', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Open interactive mesh explorer' }).click();
  const count = page.locator('.mesh-large text').last();
  const before = await count.textContent();
  const slider = page.locator('.mesh-controls input').nth(1);
  await slider.focus();
  await slider.press('End');
  await expect(page.locator('.mesh-controls output').nth(1)).toHaveText('55\u00b0');
  expect(await count.textContent()).not.toBe(before);
  await page.getByRole('button', { name: 'Reset', exact: true }).click();
  await expect(page.locator('.mesh-controls output').nth(1)).toHaveText('25\u00b0');
  await page.getByRole('button', { name: 'Close dialog' }).click();
  await expect(page.getByRole('dialog')).toHaveCount(0);
});

test('the full notebook exports as readable Markdown', async ({ page }) => {
  await page.goto('/');
  const event = page.waitForEvent('download');
  await page.locator('.export-button').click();
  const download = await event;
  expect(download.suggestedFilename()).toBe('continuum-notebook.md');
  const contents = await readFile((await download.path())!, 'utf8');
  expect(contents).toContain('# Technical corrections');
  expect(contents).toContain('reference_test.cpp');
  expect(contents).toContain('not nvcc-verified');
});

test('all main views fit the viewport', async ({ page }) => {
  for (const view of ['review', 'snippets', 'libraries', 'architecture', 'mesh', 'gpu', 'multiphysics', 'scaling', 'verification', 'saved']) {
    await page.goto(`/#${view}`);
    await expect(page.locator('main h1')).toBeVisible();
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${view} should not overflow`).toBe(true);
  }
});

test('mobile navigation closes after selecting a destination', async ({ page, isMobile }) => {
  test.skip(!isMobile);
  await page.goto('/');
  await page.getByRole('button', { name: 'Toggle navigation' }).click();
  await expect(page.locator('.sidebar')).toHaveClass(/mobile-open/);
  await page.locator('.sidebar').getByRole('button', { name: 'Library explorer', exact: true }).click();
  await expect(page.locator('.sidebar')).not.toHaveClass(/mobile-open/);
  await expect(page.getByRole('heading', { name: 'Build on good work.' })).toBeVisible();
});

test('appearance is a working local preference', async ({ page, isMobile }) => {
  await page.goto('/');
  if (isMobile) await page.getByRole('button', { name: 'Toggle navigation' }).click();
  await page.getByRole('button', { name: 'Switch to light mode' }).click();
  await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
  if (isMobile) await page.getByRole('button', { name: 'Toggle navigation' }).click();
  await page.getByRole('button', { name: 'Switch to dark mode' }).click();
  await expect(page.locator('html')).toHaveAttribute('data-theme', 'dark');
});

test('browser history restores a previous note', async ({ page }) => {
  await page.goto('/');
  await page.locator('.featured-note').first().click();
  await expect(page.locator('.article-document h1')).toHaveText('Zwei Pfade. Ein konservativer Fluss.');
  await page.locator('.next-note button').click();
  await expect(page.locator('.article-document h1')).toHaveText('Netzqualit\u00e4t ist noch kein Fehlerma\u00df.');
  await page.goBack();
  await expect(page.locator('.article-document h1')).toHaveText('Zwei Pfade. Ein konservativer Fluss.');
});