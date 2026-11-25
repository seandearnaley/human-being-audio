import path from "node:path";
const root = path.resolve(import.meta.dirname, "..");
export default {
  turbopack: { root },
  outputFileTracingRoot: root,
  poweredByHeader: false,
  images: {
    formats: ["image/avif", "image/webp"],
  },
};
