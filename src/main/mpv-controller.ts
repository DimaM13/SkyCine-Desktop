import { spawn, ChildProcess } from 'child_process';
import net from 'net';
import path from 'path';
import { EventEmitter } from 'events';
import { app } from 'electron';

export class MpvController extends EventEmitter {
  private proc: ChildProcess | null = null;
  private socket: net.Socket | null = null;
  private pipePath: string;
  private requestIdCounter = 1;
  private pendingRequests = new Map<number, (res: any) => void>();
  private isConnected = false;
  private sendQueue: string[] = [];
  private currentFile: string = '';
  private isStarting = false;

  constructor() {
    super();
    this.pipePath = `\\\\.\\pipe\\skycine_mpv_${Date.now()}_${Math.random().toString(36).substring(7)}`;
  }

  public async startPlayer(
    urlOrPath: string,
    startPos: number = 0,
    title: string = 'SkyCine Cinema',
    parentHwnd?: number | bigint
  ): Promise<void> {
    if (this.isStarting) {
      console.log('[MPV Controller] ⏳ Startup already in progress, skipping duplicate');
      return;
    }

    if (this.currentFile === urlOrPath && this.isConnected && this.proc && !this.proc.killed) {
      console.log(`[MPV Controller] ⚡ File already active in MPV: ${urlOrPath}`);
      if (startPos > 0) {
        this.seek(startPos);
      }
      return;
    }

    if (this.isConnected && this.proc && !this.proc.killed) {
      console.log(`[MPV Controller] 🔄 Loading into existing MPV process: ${urlOrPath}`);
      this.currentFile = urlOrPath;
      await this.sendCommand(['loadfile', urlOrPath, 'replace']);
      if (startPos > 0) {
        await this.sendCommand(['seek', startPos, 'absolute']);
      }
      return;
    }

    this.isStarting = true;
    this.currentFile = urlOrPath;
    this.destroy();

    this.pipePath = `\\\\.\\pipe\\skycine_mpv_${Date.now()}_${Math.random().toString(36).substring(7)}`;
    const mpvBinPath = app.isPackaged
      ? path.join(process.resourcesPath, 'bin', 'mpv.exe')
      : path.join(__dirname, '..', '..', 'bin', 'mpv.exe');
    const logFilePath = app.isPackaged
      ? path.join(app.getPath('userData'), 'mpv.log')
      : path.join(__dirname, '..', '..', 'mpv_runtime.log');

    const args = [
      `--log-file=${logFilePath}`,
      `--input-ipc-server=${this.pipePath}`,
      '--hwdec=auto-safe',
      '--vo=gpu-next',
      '--gpu-api=d3d11',
      '--osc=no',
      '--osd-level=0',
      '--osd-bar=no',
      '--input-default-bindings=no',
      '--terminal=no',
      '--input-terminal=no',
      '--idle=yes',
      '--keep-open=yes'
    ];

    if (parentHwnd) {
      args.push(`--wid=${parentHwnd.toString()}`);
    } else {
      args.push('--force-window=yes', '--no-border');
    }

    if (startPos > 0) {
      args.push(`--start=${startPos}`);
    }

    args.push(urlOrPath);

    console.log(`[MPV Controller] 🎬 Spawning embedded MPV Player (HWND: ${parentHwnd?.toString() || 'none'}) for: ${urlOrPath}`);
    this.proc = spawn(mpvBinPath, args, { windowsHide: false });

    this.proc.on('error', (err) => {
      console.error('[MPV Controller] Error spawning mpv:', err);
      this.isStarting = false;
      this.emit('error', err);
    });

    this.proc.on('close', (code) => {
      console.log('[MPV Controller] MPV process closed with code:', code);
      this.isConnected = false;
      this.isStarting = false;
      this.currentFile = '';
      this.socket?.destroy();
      this.socket = null;
      this.emit('close', code);
      this.emit('ended');
    });

    // Connect to named pipe with retries
    for (let attempt = 0; attempt < 30; attempt++) {
      try {
        await new Promise((resolve, reject) => {
          const s = net.connect(this.pipePath, () => {
            this.socket = s;
            this.isConnected = true;
            this.setupSocketListeners();
            resolve(true);
          });
          s.on('error', reject);
        });
        break;
      } catch {
        await new Promise(r => setTimeout(r, 100));
      }
    }

    this.isStarting = false;

    if (!this.isConnected) {
      console.warn('[MPV Controller] Could not establish IPC socket within timeout, playback continues standalone');
      return;
    }

    console.log('[MPV Controller] ✅ Connected to MPV IPC');

    // Register property observers
    this.observeProperty(1, 'time-pos');
    this.observeProperty(2, 'pause');
    this.observeProperty(3, 'duration');
    this.observeProperty(4, 'track-list');
    this.observeProperty(5, 'volume');
    this.observeProperty(6, 'mute');

    // Flush any queued commands
    while (this.sendQueue.length > 0) {
      const msg = this.sendQueue.shift();
      if (msg && this.socket) {
        this.socket.write(msg);
      }
    }
  }

  private setupSocketListeners(): void {
    if (!this.socket) return;

    let buffer = '';
    this.socket.on('data', (data) => {
      buffer += data.toString();
      const lines = buffer.split('\n');
      buffer = lines.pop() || '';

      for (const line of lines) {
        const trimmed = line.trim();
        if (!trimmed) continue;

        try {
          const msg = JSON.parse(trimmed);
          this.handleIpcMessage(msg);
        } catch (e) {}
      }
    });

    this.socket.on('error', (err) => {
      console.error('[MPV Controller] Socket error:', err);
    });

    this.socket.on('close', () => {
      this.isConnected = false;
    });
  }

  private handleIpcMessage(msg: any): void {
    if (msg.request_id && this.pendingRequests.has(msg.request_id)) {
      const resolver = this.pendingRequests.get(msg.request_id);
      this.pendingRequests.delete(msg.request_id);
      resolver?.(msg);
      return;
    }

    if (msg.event === 'property-change') {
      switch (msg.name) {
        case 'time-pos':
          if (typeof msg.data === 'number') {
            if (msg.data > 0.1) {
              this.emit('video-ready');
            }
            this.emit('time-update', msg.data);
          }
          break;
        case 'pause':
          if (typeof msg.data === 'boolean') {
            this.emit('play-state', !msg.data);
          }
          break;
        case 'duration':
          if (typeof msg.data === 'number') {
            this.emit('duration', msg.data);
          }
          break;
        case 'track-list':
          if (Array.isArray(msg.data)) {
            this.emit('tracks', msg.data);
          }
          break;
        case 'volume':
          if (typeof msg.data === 'number') {
            this.emit('volume', msg.data);
          }
          break;
        case 'mute':
          if (typeof msg.data === 'boolean') {
            this.emit('mute', msg.data);
          }
          break;
      }
    } else if (msg.event === 'playback-restart') {
      this.emit('video-ready');
    } else if (msg.event === 'end-file') {
      this.emit('ended');
    }
  }

  public sendCommand(command: any[]): Promise<any> {
    return new Promise((resolve) => {
      const reqId = this.requestIdCounter++;
      this.pendingRequests.set(reqId, resolve);

      const payload = JSON.stringify({ command, request_id: reqId }) + '\n';
      if (this.isConnected && this.socket) {
        this.socket.write(payload);
      } else {
        this.sendQueue.push(payload);
      }
    });
  }

  public observeProperty(id: number, property: string): void {
    const payload = JSON.stringify({ command: ['observe_property', id, property] }) + '\n';
    if (this.isConnected && this.socket) {
      this.socket.write(payload);
    } else {
      this.sendQueue.push(payload);
    }
  }

  public async play(): Promise<void> {
    await this.sendCommand(['set_property', 'pause', false]);
  }

  public async pause(): Promise<void> {
    await this.sendCommand(['set_property', 'pause', true]);
  }

  public async togglePlay(): Promise<void> {
    await this.sendCommand(['cycle', 'pause']);
  }

  public async seek(targetTime: number): Promise<void> {
    await this.sendCommand(['seek', targetTime, 'absolute']);
  }

  public async setVolume(vol: number): Promise<void> {
    await this.sendCommand(['set_property', 'volume', Math.max(0, Math.min(100, vol))]);
  }

  public async setMute(muted: boolean): Promise<void> {
    await this.sendCommand(['set_property', 'mute', muted]);
  }

  public async setAudioTrack(trackId: number): Promise<void> {
    await this.sendCommand(['set_property', 'aid', trackId]);
  }

  public async setSubtitleTrack(trackId: number | 'no'): Promise<void> {
    await this.sendCommand(['set_property', 'sid', trackId]);
  }

  public async setSpeed(speed: number): Promise<void> {
    await this.sendCommand(['set_property', 'speed', speed]);
  }

  public destroy(): void {
    this.sendQueue = [];
    this.pendingRequests.clear();
    try {
      this.socket?.destroy();
    } catch {}
    try {
      this.proc?.kill('SIGKILL');
    } catch {}
    this.proc = null;
    this.socket = null;
    this.isConnected = false;
    this.isStarting = false;
  }
}
