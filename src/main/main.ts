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
    minWidth: 800,
    minHeight: 500,
    frame: false,
    transparent: true,
    backgroundColor: '#00000000',
    resizable: true,
    movable: true,
    minimizable: true,
    maximizable: true,
    fullscreenable: true,
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

  mainWindow.on('maximize', () => {
    if (!mainWindow?.isDestroyed()) {
      mainWindow?.webContents.send('window:maximized-change', true);
    }
  });

  mainWindow.on('unmaximize', () => {
    if (!mainWindow?.isDestroyed()) {
      mainWindow?.webContents.send('window:maximized-change', false);
    }
  });

  // Toggle DevTools with F12 or Ctrl+Shift+I
  mainWindow.webContents.on('before-input-event', (event, input) => {
    if (input.key === 'F12' || (input.control && input.shift && input.key.toLowerCase() === 'i')) {
      mainWindow?.webContents.toggleDevTools();
      event.preventDefault();
    }
  });

  // Window Controls IPC Handlers
  ipcMain.handle('window:minimize', () => {
    mainWindow?.minimize();
  });

  ipcMain.handle('window:maximize', () => {
    if (mainWindow?.isMaximized()) {
      mainWindow.unmaximize();
    } else {
      mainWindow?.maximize();
    }
  });

  ipcMain.handle('window:close', () => {
    mainWindow?.close();
  });

  ipcMain.handle('window:isMaximized', () => {
    return mainWindow?.isMaximized() || false;
  });

  ipcMain.handle('window:toggleFullscreen', async () => {
    if (mainWindow) {
      mainWindow.setFullScreen(!mainWindow.isFullScreen());
    }
  });

  // Forward MPV events to React UI
  mpv.on('video-ready', () => {
    if (!mainWindow?.isDestroyed()) {
      mainWindow?.webContents.send('mpv:video-ready');
    }
  });

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

  // Setup MPV IPC Handlers
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
