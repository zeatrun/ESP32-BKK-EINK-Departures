import fs from 'fs-extra';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

async function copyToDat() {
  const distPath = path.join(__dirname, '..', 'dist');
  const dataPath = path.join(__dirname, '..', '..', 'data', 'config-app');

  try {
    // Remove old build if exists
    if (fs.existsSync(dataPath)) {
      fs.removeSync(dataPath);
    }

    // Copy new build
    fs.copySync(distPath, dataPath);

    console.log(`✓ Build copied to ${dataPath}`);
    
    // Log file sizes
    const files = fs.readdirSync(dataPath, { recursive: true });
    let totalSize = 0;
    files.forEach(file => {
      const fullPath = path.join(dataPath, file);
      if (fs.statSync(fullPath).isFile()) {
        totalSize += fs.statSync(fullPath).size;
      }
    });
    
    console.log(`Total size: ${(totalSize / 1024).toFixed(2)} KB`);
  } catch (err) {
    console.error('Error copying build:', err);
    process.exit(1);
  }
}

copyToDat();
