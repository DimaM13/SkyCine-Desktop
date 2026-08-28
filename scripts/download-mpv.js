const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const binDir = path.join(__dirname, '..', 'bin');
const mpvExe = path.join(binDir, 'mpv.exe');

if (fs.existsSync(mpvExe)) {
  console.log('✅ MPV binary is already installed at:', mpvExe);
  process.exit(0);
}

if (!fs.existsSync(binDir)) {
  fs.mkdirSync(binDir, { recursive: true });
}

console.log('📦 Downloading static MPV binary for Windows...');
const mpvZipUrl = 'https://sourceforge.net/projects/mpv-player-windows/files/64bit-v3/mpv-x86_64-v3-20260828-git-182fa6c.7z/download';
const zipDest = path.join(binDir, 'mpv-archive.7z');

try {
  execSync(`powershell -Command "Invoke-WebRequest -Uri '${mpvZipUrl}' -OutFile '${zipDest}'"`, { stdio: 'inherit' });
  console.log('📦 Extracting MPV...');
  execSync(`powershell -Command "Expand-Archive -Path '${zipDest}' -DestinationPath '${binDir}' -Force"`, { stdio: 'inherit' });
  if (fs.existsSync(zipDest)) fs.unlinkSync(zipDest);
  console.log('✅ MPV downloaded and installed successfully!');
} catch (err) {
  console.warn('⚠️ Automated MPV download failed, ensure mpv.exe is placed in bin/');
}
