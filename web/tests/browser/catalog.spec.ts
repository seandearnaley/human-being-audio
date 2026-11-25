import { expect, test } from "@playwright/test";
test("browse the studio products, inspect images and open install notes", async ({
  page,
  request,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("/");
  await expect(page.getByRole("heading", { level: 1 })).toContainText(
    "Human Being Audio.",
  );
  const studioMark = page.locator(".site-header .wordmark img");
  const tabIcon = await page.locator('link[rel="icon"]').getAttribute("href");
  expect(await studioMark.count()).toBe(1);
  await expect(studioMark).toHaveAttribute("src", tabIcon!);
  await page.getByRole("link", { name: "Browse the plugins" }).click();
  const cards = page.locator("article[data-plugin]");
  await expect(cards).toHaveCount(6);
  for (const [index, slug, name] of [
    [0, "livecut-redux", "Livecut Redux"],
    [1, "tract-form", "Tract Form"],
    [2, "pole-field", "Pole Field"],
    [3, "cutline", "Cutline"],
    [4, "torus-glide", "Torus Glide"],
    [5, "strike-body", "Strike Body"],
  ] as const) {
    await expect(page.locator('meta[name="description"]')).toHaveAttribute("content", new RegExp(name));
    const card = cards.nth(index);
    await expect(
      card.getByRole("heading", { name, exact: true }),
    ).toBeVisible();
    await expect(
      card.getByRole("link", { name: "Source and build instructions" }),
    ).toHaveAttribute(
      "href",
      "https://github.com/seandearnaley/human-being-audio/tree/main/products/" +
        slug,
    );
    const image = card.getByRole("img");
    await image.scrollIntoViewIfNeeded();
    await expect
      .poll(() => image.evaluate((el) => (el as HTMLImageElement).naturalWidth))
      .toBeGreaterThan(0);
  }
  await expect(page.locator("body")).not.toContainText(
    /polyrift|glassline|human being music/i,
  );
  const links = await page
    .locator("a[href]")
    .evaluateAll((elements) =>
      elements.map((el) => (el as HTMLAnchorElement).href),
    );
  expect(
    links.filter((url) =>
      /github\.com\/seandearnaley\/(?!human-being-audio(?:\/|$))/.test(url),
    ),
  ).toEqual([]);
  for (const id of ["macOS", "Windows", "Ubuntu", "Verify a download"]) {
    const detail = page
      .locator("#install details")
      .filter({ has: page.getByText(id, { exact: true }) });
    await detail.locator("summary").click();
    await expect(detail).toHaveAttribute("open", "");
  }
  await expect(page.locator("#install")).toContainText("~/Library/Audio/Plug-Ins");
  const viewport = page.viewportSize()!;
  const widths = test.info().project.name === "mobile" ? [320, 390, viewport.width] : [viewport.width];
  for (const width of widths) {
    await page.setViewportSize({ width, height: viewport.height });
    await page.evaluate(() => document.fonts.ready);
    expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(width);
  }
  const response = await request.get("/api/releases");
  expect(response.ok()).toBe(true);
  for (const release of await response.json()) {
    const card = page.locator("#" + release.product);
    for (const asset of release.downloads)
      await expect(
        card.locator("a").filter({
          hasText:
            asset.os === "linux"
              ? "Ubuntu"
              : asset.os === "windows"
                ? "Windows"
                : "macOS",
        }),
      ).toHaveAttribute("href", asset.url);
  }
  expect(errors).toEqual([]);
});
test("all showcase decks decode, switch tracks, and play exclusively on request", async ({
  page,
}) => {
  await page.goto("/");
  const decks = page.locator("figure[data-showcase]");
  await expect(decks).toHaveCount(6);
  await expect(page.locator(".showcase__track")).toHaveCount(36);

  const examples = page.locator("audio");
  await expect(examples).toHaveCount(6);
  for (const example of await examples.all()) {
    await expect(example).toHaveAttribute("preload", "none");
    expect(await example.evaluate((el: HTMLAudioElement) => el.paused)).toBe(true);
    const duration = await example.evaluate(async (el: HTMLAudioElement) => {
      await el.play();
      el.pause();
      return el.duration;
    });
    expect(duration).toBeGreaterThan(4.5);
    expect(duration).toBeLessThan(15);
  }

  // Switch track inside Tract Form and verify src + active title + exclusive playback
  const tractDeck = page.locator('figure[data-showcase="tract-form"]');
  const poleDeck = page.locator('figure[data-showcase="pole-field"]');
  await tractDeck.locator(".showcase__track").nth(1).click();
  await expect(tractDeck.locator(".showcase__title")).toHaveText("Acid Snap Sequence");
  await expect(tractDeck.locator("audio")).toHaveAttribute(
    "src",
    "/audio/tract-form/02-acid-snap-sequence.mp3",
  );
  await expect(tractDeck).toHaveAttribute("data-playing", "true");

  // Starting Pole Field automatically pauses Tract Form
  await poleDeck.locator(".showcase__play").click();
  await expect(poleDeck).toHaveAttribute("data-playing", "true");
  await expect(tractDeck).toHaveAttribute("data-playing", "false");

  // Pause Pole Field via its toggling Pause button (no aria-pressed on label-toggling play/pause)
  const polePlayBtn = poleDeck.locator(".showcase__play");
  await expect(polePlayBtn).not.toHaveAttribute("aria-pressed");
  await polePlayBtn.click();
  await expect(poleDeck).toHaveAttribute("data-playing", "false");
});

test("the sculpture responds to keyboard controls", async ({ page }) => {
  await page.goto("/");
  const slider = page.getByRole("slider", { name: "Chaos" });
  await expect(slider).toHaveAttribute("aria-valuetext", "15 percent");
  await slider.focus();
  await slider.press("ArrowRight");
  await expect(slider).toHaveAttribute("aria-valuetext", "16 percent");
  await page.getByRole("button", { name: "Freeze" }).click();
  await expect(page.getByRole("button", { name: "Freeze" })).toHaveAttribute(
    "aria-pressed",
    "true",
  );
});
test("catalogue and navigation work without JavaScript", async ({
  browser,
  baseURL,
}) => {
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();
  await page.goto(baseURL!);
  await expect(page.locator("article[data-plugin]")).toHaveCount(6);
  await expect(page.locator("audio[controls]")).toHaveCount(6);
  await expect(page.locator("audio[controls]").first()).toBeVisible();
  await expect(
    page.getByRole("link", { name: "Source and build instructions" }),
  ).toHaveCount(6);
  const hashLinks = await page
    .locator('a[href^="#"]')
    .evaluateAll((links) => links.map((el) => el.getAttribute("href")!));
  for (const id of hashLinks) await expect(page.locator(id)).toHaveCount(1);
  await context.close();
});
