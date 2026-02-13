import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

export default defineConfig({
  plugins: [svelte()],
  build: {
    outDir: '../build/www',
    emptyOutDir: true,
    // Keep bundle small for ESP32 SPIFFS (1MB partition)
    minify: 'esbuild',
    rollupOptions: {
      output: {
        // Single chunk for simplicity on embedded
        manualChunks: undefined,
      },
    },
  },
  server: {
    proxy: {
      '/api': 'http://djconsole.local',
      '/ws': { target: 'ws://djconsole.local', ws: true },
    },
  },
});
