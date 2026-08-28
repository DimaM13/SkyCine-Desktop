import { contextBridge, ipcRenderer } from 'electron';

export interface DesktopPlayerApi {
  isDesktop: boolean;
  loadFile: (url: string, startPos?: number, title?: string) => Promise<void>;
  play: () => Promise<void>;
  pause: () => Promise<void>;
  togglePlay: () => Promise<void>;
  seek: (targetTime: number) => Promise<void>;
  setVolume: (vol: number) => Promise<void>;
  setMute: (muted: boolean) => Promise<void>;
  setAudioTrack: (id: number) => Promise<void>;
  setSubtitleTrack: (id: number | 'no') => Promise<void>;
  setSpeed: (speed: number) => Promise<void>;
  showOsdText: (text: string, durationMs?: number) => Promise<void>;
  closePlayer: () => Promise<void>;
  toggleFullscreen: () => Promise<void>;
  minimizeWindow: () => Promise<void>;
  maximizeWindow: () => Promise<void>;
  closeWindow: () => Promise<void>;
  isMaximized: () => Promise<boolean>;
  onMaximizedChange: (callback: (isMax: boolean) => void) => () => void;
  onVideoReady: (callback: () => void) => () => void;
  onTimeUpdate: (callback: (time: number) => void) => () => void;
  onPlayState: (callback: (isPlaying: boolean) => void) => () => void;
  onDuration: (callback: (duration: number) => void) => () => void;
  onBuffering: (callback: (isBuffering: boolean) => void) => () => void;
  onTracks: (callback: (tracks: any[]) => void) => () => void;
  onEnded: (callback: () => void) => () => void;
}

const desktopPlayer: DesktopPlayerApi = {
  isDesktop: true,
  loadFile: (url, startPos = 0, title = 'SkyCine Cinema') => ipcRenderer.invoke('mpv:loadFile', url, startPos, title),
  play: () => ipcRenderer.invoke('mpv:play'),
  pause: () => ipcRenderer.invoke('mpv:pause'),
  togglePlay: () => ipcRenderer.invoke('mpv:togglePlay'),
  seek: (targetTime) => ipcRenderer.invoke('mpv:seek', targetTime),
  setVolume: (vol) => ipcRenderer.invoke('mpv:setVolume', vol),
  setMute: (muted) => ipcRenderer.invoke('mpv:setMute', muted),
  setAudioTrack: (id) => ipcRenderer.invoke('mpv:setAudioTrack', id),
  setSubtitleTrack(id) { return ipcRenderer.invoke('mpv:setSubtitleTrack', id); },
  setSpeed: (speed) => ipcRenderer.invoke('mpv:setSpeed', speed),
  showOsdText: (text, durationMs = 2000) => ipcRenderer.invoke('mpv:showOsd', text, durationMs),
  closePlayer: () => ipcRenderer.invoke('mpv:close'),
  toggleFullscreen: () => ipcRenderer.invoke('window:toggleFullscreen'),
  minimizeWindow: () => ipcRenderer.invoke('window:minimize'),
  maximizeWindow: () => ipcRenderer.invoke('window:maximize'),
  closeWindow: () => ipcRenderer.invoke('window:close'),
  isMaximized: () => ipcRenderer.invoke('window:isMaximized'),

  onMaximizedChange: (callback) => {
    const handler = (_: any, isMax: boolean) => callback(isMax);
    ipcRenderer.on('window:maximized-change', handler);
    return () => ipcRenderer.removeListener('window:maximized-change', handler);
  },

  onVideoReady: (callback) => {
    const handler = () => callback();
    ipcRenderer.on('mpv:video-ready', handler);
    return () => ipcRenderer.removeListener('mpv:video-ready', handler);
  },

  onTimeUpdate: (callback) => {
    const handler = (_: any, time: number) => callback(time);
    ipcRenderer.on('mpv:time-update', handler);
    return () => ipcRenderer.removeListener('mpv:time-update', handler);
  },
  onPlayState: (callback) => {
    const handler = (_: any, isPlaying: boolean) => callback(isPlaying);
    ipcRenderer.on('mpv:play-state', handler);
    return () => ipcRenderer.removeListener('mpv:play-state', handler);
  },
  onDuration: (callback) => {
    const handler = (_: any, duration: number) => callback(duration);
    ipcRenderer.on('mpv:duration', handler);
    return () => ipcRenderer.removeListener('mpv:duration', handler);
  },
  onBuffering: (callback) => {
    const handler = (_: any, isBuffering: boolean) => callback(isBuffering);
    ipcRenderer.on('mpv:buffering', handler);
    return () => ipcRenderer.removeListener('mpv:buffering', handler);
  },
  onTracks: (callback) => {
    const handler = (_: any, tracks: any[]) => callback(tracks);
    ipcRenderer.on('mpv:tracks', handler);
    return () => ipcRenderer.removeListener('mpv:tracks', handler);
  },
  onEnded: (callback) => {
    const handler = () => callback();
    ipcRenderer.on('mpv:ended', handler);
    return () => ipcRenderer.removeListener('mpv:ended', handler);
  }
};

contextBridge.exposeInMainWorld('desktopPlayer', desktopPlayer);
