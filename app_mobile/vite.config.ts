import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  base: './',
  publicDir: 'public',
  build: {
    outDir: 'www',
    emptyOutDir: true,
    target: 'es2020',
    minify: 'esbuild'
  }
});

