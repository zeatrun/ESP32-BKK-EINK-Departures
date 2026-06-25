import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  build: {
    outDir: '../data/config-app',
    emptyOutDir: true,
    minify: 'terser',
    target: 'ES2020',
  },
  server: {
    proxy: {
      '/api': 'http://192.168.4.1'
    }
  }
})
