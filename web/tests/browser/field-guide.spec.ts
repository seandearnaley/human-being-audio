import { expect, test } from "@playwright/test";

test("navigate to Field Guides hub and open Pole Field interactive masterclass", async ({
  page,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });

  await page.goto("/learn");
  await expect(page.getByRole("heading", { level: 1 })).toContainText("DSP Field Guides.");
  await page.getByRole("link", { name: /Open Interactive Field Guide/i }).click();
  await expect(page).toHaveURL(/\/learn\/pole-field$/);
  await expect(page.getByRole("heading", { level: 1 })).toContainText("Painting With Poles.");

  // Depth switcher toggles Layer 1 / Layer 2 cleanly
  await expect(page.getByText("Layer 1 · The Musical Ear").first()).toBeVisible();
  await expect(page.getByText("Layer 2 · Under the Hood").first()).toBeVisible();

  await page.getByRole("button", { name: "Musician", exact: true }).click();
  await expect(page.getByText("Layer 1 · The Musical Ear").first()).toBeVisible();
  await expect(page.getByText("Layer 2 · Under the Hood")).toHaveCount(0);

  await page.getByRole("button", { name: "DSP Engineer", exact: true }).click();
  await expect(page.getByText("Layer 1 · The Musical Ear")).toHaveCount(0);
  await expect(page.getByText("Layer 2 · Under the Hood").first()).toBeVisible();

  await page.getByRole("button", { name: "Both Layers", exact: true }).click();

  // Lab 1: slider adjusts Pole Frequency live
  const poleSlider = page.locator("#l1-phz");
  await poleSlider.fill("1850");
  await expect(poleSlider).toHaveAttribute("aria-valuetext", "1850 Hz");

  // Lab 3: one-click Producer Recipe updates cube & topology, and XY Morph Pad responds to keyboard arrows
  await page.getByRole("button", { name: "Hollow Phase Razor" }).click();
  await expect(page.getByRole("button", { name: "Phase Razor", exact: true })).toHaveAttribute(
    "aria-pressed",
    "true",
  );
  await expect(page.getByRole("button", { name: "Hybrid", exact: true })).toHaveAttribute(
    "aria-pressed",
    "true",
  );
  const morphPad = page.getByRole("slider", { name: /Interactive 2D XY morph pad/i });
  await morphPad.focus();
  await page.keyboard.press("ArrowRight");
  await expect(page.locator("#l3-x")).toHaveAttribute("aria-valuetext", "73%");

  // Lab 4: AudioWorklet ADAA2 tone starts cleanly without minification/scope ReferenceErrors
  const adaaPlay = page.getByRole("button", { name: /Hear Aliasing A\/B/i });
  await adaaPlay.click();
  await expect(page.getByRole("button", { name: /Stop Tone/i })).toHaveAttribute(
    "aria-pressed",
    "true",
  );
  await page.getByRole("button", { name: /Stop Tone/i }).click();

  // Lab 5: live Burg LPC extraction switches targets without error
  await page.getByRole("button", { name: "Glass Bell" }).click();
  await expect(page.getByText("Lab 5 · Glass Bell (built-in)")).toBeVisible();

  // Zero horizontal overflow at current & 320px mobile width
  const viewport = page.viewportSize()!;
  const widths =
    test.info().project.name === "mobile" ? [320, 390, viewport.width] : [viewport.width];
  for (const width of widths) {
    await page.setViewportSize({ width, height: viewport.height });
    await page.evaluate(() => document.fonts.ready);
    expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(width);
  }

  expect(errors).toEqual([]);
});
