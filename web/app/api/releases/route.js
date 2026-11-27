import { readCatalog } from "../../../lib/releases.mjs";
export const revalidate = 60;
export async function GET() {
  try {
    const { releases, errors } = await readCatalog();
    if (errors.size) throw new AggregateError(errors.values(), "Invalid studio releases");
    return Response.json(releases);
  } catch (error) {
    console.error(error);
    return Response.json(
      { error: "Studio release data is unavailable" },
      { status: 502, headers: { "Cache-Control": "no-store" } },
    );
  }
}
