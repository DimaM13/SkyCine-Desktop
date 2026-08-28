import { app, BrowserWindow, ipcMain } from 'electron';
import path from 'path';
import { MpvController } from './mpv-controller';

let mainWindow: BrowserWindow | null = null;
let mpv: MpvController | null = null;

async function createWindow() {
  const preloadPath = path.join(__dirname, '..', 'preload', 'preload.js');

  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    minWidth: 1024,
    minHeight: 600,
    backgroundColor: '#00000000',
    transparent: true,
    autoHideMenuBar: true,
    title: 'SkyCine Cinema Desktop',
    webPreferences: {
      preload: preloadPath,
      nodeIntegration: false,
      contextIsolation: true,
      webSecurity: false
    }
  });

  mpv = new MpvController();

  mainWindow.on('minimize', () => {
    mpv?.pause();
  });

  // Forward MPV events to React UI
  mpv.on('time-update', (time) => {
    if (!mainWindow?.isDestroyed()) {
      mainWindow?.webContents.send('mpv:time-update', time);
    }
  });

  mpv.on('play-state', (isPlaying) => {
    if (!mainWindow?.isDestroyed()) {
      mainWindow?.webContents.send('mpv:play-state', isPlaying);
    }
  });

  mpv.on('duration', (duration) => {
    if (!mainWindow?.isDestroyed()) {
      mainWindow?.webContents.send('mpv:duration', duration);
    }
  });

  mpv.on('buffering', (isBuffering) => {
    if (!mainWindow?.isDestroyed()) {
      mainWindow?.webContents.send('mpv:buffering', isBuffering);
    }
  });

  mpv.on('tracks', (tracks) => {
    if (!mainWindow?.isDestroyed()) {
      mainWindow?.webContents.send('mpv:tracks', tracks);
    }
  });

  mpv.on('ended', () => {
    if (!mainWindow?.isDestroyed()) {
      mainWindow?.webContents.send('mpv:ended');
    }
  });

  // Setup IPC Handlers
  ipcMain.handle('mpv:loadFile', async (_, url, startPos, title) => {
    const parentHwnd = mainWindow?.getNativeWindowHandle().readBigInt64LE(0);
    await mpv?.startPlayer(url, startPos, title, parentHwnd);
  });

  ipcMain.handle('mpv:play', async () => {
    await mpv?.play();
  });

  ipcMain.handle('mpv:pause', async () => {
    await mpv?.pause();
  });

  ipcMain.handle('mpv:togglePlay', async () => {
    await mpv?.togglePlay();
  });

  ipcMain.handle('mpv:seek', async (_, targetTime) => {
    await mpv?.seek(targetTime);
  });

  ipcMain.handle('mpv:setVolume', async (_, vol) => {
    await mpv?.setVolume(vol);
  });

  ipcMain.handle('mpv:setMute', async (_, muted) => {
    await mpv?.setMute(muted);
  });

  ipcMain.handle('mpv:setAudioTrack', async (_, id) => {
    await mpv?.setAudioTrack(id);
  });

  ipcMain.handle('mpv:setSubtitleTrack', async (_, id) => {
    await mpv?.setSubtitleTrack(id);
  });

  ipcMain.handle('mpv:setSpeed', async (_, speed) => {
    await mpv?.setSpeed(speed);
  });

  ipcMain.handle('mpv:close', async () => {
    mpv?.destroy();
  });

  const prodPath = path.join(__dirname, '..', 'renderer', 'index.html');

  if (!app.isPackaged) {
    try {
      await mainWindow.loadURL('http://localhost:3000');
    } catch {
      mainWindow.loadFile(prodPath);
    }
  } else {
    mainWindow.loadFile(prodPath);
  }

  mainWindow.on('closed', () => {
    mainWindow = null;
    mpv?.destroy();
    mpv = null;
  });
}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    mpv?.destroy();
    app.quit();
  }
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});
