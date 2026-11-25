import { readCatalog } from "../../../lib/releases.mjs";
export const revalidate = 60;
export async function GET() {
  try {
    return Response.json(await readCatalog());
  } catch (error) {
    console.error(error);
    return Response.json(
      { error: "Studio release data is unavailable" },
      { status: 502, headers: { "Cache-Control": "no-store" } },
    );
  }
}
