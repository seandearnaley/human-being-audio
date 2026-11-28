import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The app's page only. The engine (the output frame) is plain files and is never bundled.
export default defineConfig({ base: "./", plugins: [react()], build: { outDir: "dist", emptyOutDir: true } });
