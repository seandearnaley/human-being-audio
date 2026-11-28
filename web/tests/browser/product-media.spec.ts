import { expect, test } from "@playwright/test";

test("the Livecut Redux demo loads only on request, takes turns with the audio deck, and the gallery opens full-size shots", async ({
  page,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  const videoRequests: string[] = [];
  page.on("request", (r) => {
    if (/\.(mp4|webm)(\?|$)/.test(r.url())) videoRequests.push(r.url());
  });

  await page.goto("/");
  const card = page.locator("#livecut-redux");
  const play = card.getByRole("link", { name: "Play the 0:46 Livecut Redux demo video, with sound" });
  await play.scrollIntoViewIfNeeded();
  await expect(play).toHaveAttribute("href", "/media/livecut-redux/livecut-redux-demo.mp4");
  const poster = card.locator("img.studio-interface");
  await expect.poll(() => poster.evaluate((el) => (el as HTMLImageElement).naturalWidth)).toBeGreaterThan(0);
  await expect(card.locator("video")).toHaveCount(0);
  expect(videoRequests).toEqual([]);

  const deck = page.locator('figure[data-showcase="livecut-redux"]');
  await deck.locator(".showcase__play").click();
  await expect(deck).toHaveAttribute("data-playing", "true");

  await play.click();
  const video = card.locator("video");
  await expect(video).toBeVisible();
  await expect.poll(() => video.evaluate((v: HTMLVideoElement) => v.currentTime)).toBeGreaterThan(0.3);
  expect(await video.evaluate((v: HTMLVideoElement) => !v.paused && !v.muted && v.duration > 40)).toBe(true);
  await expect(deck).toHaveAttribute("data-playing", "false");
  // Media keys and the lock screen now drive the video, not the deck that played before it.
  expect(await page.evaluate(() => navigator.mediaSession.metadata?.title)).toBe("Livecut Redux in Ableton Live 12");

  await deck.locator(".showcase__play").click();
  await expect.poll(() => video.evaluate((v: HTMLVideoElement) => v.paused)).toBe(true);

  const thumbs = card.getByRole("list", { name: "Livecut Redux screenshots" }).getByRole("link");
  await expect(thumbs).toHaveCount(8);
  await thumbs.nth(1).click();
  const dialog = page.getByRole("dialog", { name: "Livecut Redux screenshots" });
  await expect(dialog).toBeVisible();
  await expect(dialog).toContainText("2 / 8 · Pad · Cut");
  await expect(dialog.getByRole("figure")).toBeFocused();
  await expect(dialog.locator("figcaption")).toHaveAttribute("aria-live", "polite");
  await dialog.locator("figcaption").click();
  await expect(dialog).toBeVisible();
  const shot = dialog.getByRole("img");
  // naturalWidth is density-corrected, so check the width of the file the browser chose instead.
  const chosenWidth = () =>
    shot.evaluate((el) => {
      const img = el as HTMLImageElement;
      return img.complete && img.naturalWidth > 0 ? Number(new URL(img.currentSrc).searchParams.get("w")) : 0;
    });
  await expect.poll(chosenWidth).toBeGreaterThanOrEqual(1000);
  await page.keyboard.press("ArrowRight");
  await expect(dialog).toContainText("3 / 8 · Pad · Tape");
  await page.keyboard.press("Escape");
  await expect(dialog).toBeHidden();
  await thumbs.first().click();
  await expect(dialog).toBeVisible();
  await page.mouse.click(4, 4); // the backdrop
  await expect(dialog).toBeHidden();
  expect(errors).toEqual([]);
});

test("without JavaScript the demo and screenshots are plain links to the files", async ({ browser, baseURL }) => {
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage();
  await page.goto(baseURL!);
  const card = page.locator("#livecut-redux");
  await expect(card.getByRole("link", { name: /demo video/ })).toHaveAttribute(
    "href",
    "/media/livecut-redux/livecut-redux-demo.mp4",
  );
  const first = card.getByRole("list", { name: "Livecut Redux screenshots" }).getByRole("link").first();
  await expect(first).toHaveAttribute("href", "/media/livecut-redux/gallery/01-live-session-pad.webp");
  expect((await page.request.get("/media/livecut-redux/livecut-redux-demo.mp4")).ok()).toBe(true);
  await context.close();
});
