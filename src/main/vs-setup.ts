import fs from 'fs';
import path from 'path';
import { execSync } from 'child_process';

interface PythonInfo {
  pythonExe: string;
  pythonDll: string;
  mtime: number;
}

export function findPythonInfo(binDir?: string): PythonInfo | null {
  // 1. First priority: Check for bundled portable Python inside bin/python
  if (binDir) {
    const bundledPythonDir = path.join(binDir, 'python');
    const bundledExe = path.join(bundledPythonDir, 'python.exe');
    if (fs.existsSync(bundledExe)) {
      try {
        const files = fs.readdirSync(bundledPythonDir);
        const dll = files.find(f => /^python3\d+\.dll$/i.test(f));
        if (dll) {
          const stat = fs.statSync(bundledExe);
          console.log('[VapourSynth Setup] 📦 Using bundled portable Python:', bundledExe);
          return {
            pythonExe: bundledExe,
            pythonDll: path.join(bundledPythonDir, dll),
            mtime: Math.floor(stat.mtimeMs / 1000)
          };
        }
      } catch {}
    }
  }

  // 2. Second priority: Check standard system locations
  const candidateDirs: string[] = [
    'C:\\Program Files\\Python312',
    'C:\\Program Files\\Python311',
    'C:\\Program Files\\Python310',
    path.join(process.env.LOCALAPPDATA || '', 'Programs', 'Python', 'Python312'),
    path.join(process.env.LOCALAPPDATA || '', 'Programs', 'Python', 'Python311'),
    path.join(process.env.LOCALAPPDATA || '', 'Programs', 'Python', 'Python310'),
  ];

  // Try where.exe python without showing a console window
  try {
    const whereOut = execSync('where.exe python', {
      encoding: 'utf-8',
      stdio: ['pipe', 'pipe', 'ignore'],
      windowsHide: true
    });
    const lines = whereOut.split('\r\n').map(l => l.trim()).filter(Boolean);
    for (const line of lines) {
      if (!line.includes('WindowsApps') && fs.existsSync(line)) {
        candidateDirs.unshift(path.dirname(line));
      }
    }
  } catch {}

  for (const dir of candidateDirs) {
    const exe = path.join(dir, 'python.exe');
    if (fs.existsSync(exe)) {
      try {
        const files = fs.readdirSync(dir);
        const dll = files.find(f => /^python3\d+\.dll$/i.test(f));
        if (dll) {
          const stat = fs.statSync(exe);
          console.log('[VapourSynth Setup] 💻 Using system Python:', exe);
          return {
            pythonExe: exe,
            pythonDll: path.join(dir, dll),
            mtime: Math.floor(stat.mtimeMs / 1000)
          };
        }
      } catch {}
    }
  }

  return null;
}

export function ensureVapourSynthConfig(binDir: string): boolean {
  if (process.platform !== 'win32') return false;

  const appData = process.env.APPDATA;
  if (!appData) return false;

  const vsConfigDir = path.join(appData, 'vapoursynth');
  const vsConfigFile = path.join(vsConfigDir, 'vapoursynth.toml');

  const pyInfo = findPythonInfo(binDir);
  if (!pyInfo) {
    console.warn('[VapourSynth Setup] Python installation not found for VapourSynth');
    return false;
  }

  const vsscriptDll = path.join(binDir, 'vapoursynth', 'vsscript.dll');
  const vsscriptRootDll = path.join(binDir, 'vsscript.dll');

  const keys = [vsscriptDll.toLowerCase(), vsscriptRootDll.toLowerCase()];

  try {
    if (!fs.existsSync(vsConfigDir)) {
      fs.mkdirSync(vsConfigDir, { recursive: true });
    }

    let lines: string[] = [];
    if (fs.existsSync(vsConfigFile)) {
      lines = fs.readFileSync(vsConfigFile, 'utf-8').split('\n').map(l => l.trim()).filter(Boolean);
    }

    // Filter out obsolete temp paths that no longer exist
    lines = lines.filter(line => {
      const match = line.match(/^"([^"]+)"/);
      if (!match) return true;
      const dllPath = match[1].replace(/\\\\/g, '\\');
      if (/appdata\\local\\temp/i.test(dllPath)) {
        return fs.existsSync(dllPath);
      }
      return true;
    });

    const escapedExe = pyInfo.pythonExe.replace(/\\/g, '\\\\');
    const escapedDll = pyInfo.pythonDll.replace(/\\/g, '\\\\');
    // NOTE: vsscript.dll requires the 3rd element (mtime) to be a quoted string in TOML!
    const valueStr = `["${escapedExe}","${escapedDll}","${pyInfo.mtime}"]`;

    for (const k of keys) {
      const escapedKey = `"${k.replace(/\\/g, '\\\\')}"`;
      const newLine = `${escapedKey} = ${valueStr}`;

      const existingIndex = lines.findIndex(l => l.startsWith(escapedKey) || l.startsWith(`"${k}"`));
      if (existingIndex >= 0) {
        lines[existingIndex] = newLine;
      } else {
        lines.push(newLine);
      }
    }

    fs.writeFileSync(vsConfigFile, lines.join('\n') + '\n', 'utf-8');
    console.log('[VapourSynth Setup] ✅ Configured vapoursynth.toml successfully with keys:', keys);
    runGpuBenchmarkIfNeeded(binDir);
    return true;
  } catch (err) {
    console.error('[VapourSynth Setup] Failed to configure vapoursynth.toml:', err);
    return false;
  }
}

export function runGpuBenchmarkIfNeeded(binDir: string): void {
  if (process.platform !== 'win32') return;

  const appData = process.env.APPDATA;
  if (!appData) return;

  const benchFile = path.join(appData, 'vapoursynth', 'rife_benchmark.json');
  if (fs.existsSync(benchFile)) {
    try {
      const content = fs.readFileSync(benchFile, 'utf-8');
      const data = JSON.parse(content);
      if (data && data.timings && Object.keys(data.timings).length > 0) {
        console.log('[RIFE Benchmark] ⚡ Existing GPU benchmark found:', data.gpu);
        return;
      }
    } catch {}
  }

  const pyInfo = findPythonInfo(binDir);
  if (!pyInfo) return;

  const benchScript = path.join(binDir, 'vapoursynth', 'benchmark_gpu.py');
  if (!fs.existsSync(benchScript)) return;

  console.log('[RIFE Benchmark] 🚀 Spawning background GPU benchmark (silent)...');
  try {
    const { spawn } = require('child_process');
    const child = spawn(pyInfo.pythonExe, [benchScript], {
      windowsHide: true,
      stdio: ['ignore', 'ignore', 'ignore'],
      detached: true
    });
    child.unref();
  } catch (e) {
    console.warn('[RIFE Benchmark] Could not spawn background benchmark:', e);
  }
}
