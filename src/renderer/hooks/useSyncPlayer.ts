import { useState, useEffect, useRef, useCallback } from 'react';
import { useSocket } from '../context/SocketContext';
import { useAuth } from '../context/AuthContext';
import { Room, RoomMember, RoomChatMessage, RoomReaction, RoomState, RoomHealthUpdate, RoomHealthEntry, RoomActionFeedEntry, RoomRollbackNotice } from '../types';

interface UseSyncPlayerProps {
  room: Room | null;
  videoRef?: React.RefObject<HTMLVideoElement | null>;
  onSeekTo?: (pos: number, shouldPlay?: boolean) => void;
  onPlay?: () => void;
  onPause?: () => void;
  getCurrentTime?: () => number;
  getIsPaused?: () => boolean;
}

export function useSyncPlayer({
  room,
  videoRef,
  onSeekTo,
  onPlay,
  onPause,
  getCurrentTime,
  getIsPaused,
}: UseSyncPlayerProps) {
  const { socket, getSyncedServerTime, getRtt } = useSocket();
  const { user } = useAuth();

  const [roomState, setRoomState] = useState<RoomState>(room?.state || 'PAUSED');
  const [members, setMembers] = useState<RoomMember[]>([]);
  const [messages, setMessages] = useState<RoomChatMessage[]>([]);
  const [reactions, setReactions] = useState<RoomReaction[]>([]);
  const [syncDiffSec, setSyncDiffSec] = useState<number>(0);
  const [isHost, setIsHost] = useState(false);
  // ── Room Health ──
  const [health, setHealth] = useState<RoomHealthEntry[]>([]);
  const [culpritIds, setCulpritIds] = useState<string[]>([]);
  const [waitingFor, setWaitingFor] = useState<string[]>([]);
  const [waitingText, setWaitingText] = useState<string | null>(null);
  const [actionFeed, setActionFeed] = useState<RoomActionFeedEntry[]>([]);
  const [rollbackNotice, setRollbackNotice] = useState<RoomRollbackNotice | null>(null);

  const isBufferingRef = useRef<boolean>(false);
  const stallCountRef = useRef<number>(0);
  const stallMsRef = useRef<number>(0);
  const stallStartRef = useRef<number>(0);
  const droppedFramesRef = useRef<number>(0);
  const droppedBaseRef = useRef<number>(0);
  const lastBufferingEmitRef = useRef<number>(0);
  const mpvAheadRef = useRef<number>(0);
  const mpvAheadSeenRef = useRef<boolean>(false);
  const mpvHwdecRef = useRef<string | undefined>(undefined);

  const roomStateRef = useRef<RoomState>(room?.state || 'PAUSED');
  const isInternalAction = useRef<boolean>(false);
  const scheduledPlayTimer = useRef<NodeJS.Timeout | null>(null);

  const internalActionTimer = useRef<NodeJS.Timeout | null>(null);
  const lastSentSeekPosRef = useRef<number | null>(null);
  const lastSentSeekTimeRef = useRef<number>(0);
  const lastJoinedUserIdRef = useRef<string | null>(null);

  // Блокировка обработки входящих якорей на время своих действий.
  // ВАЖНО: sendPlay/sendPause НЕ дропаются этим флагом (раньше пауза молча глоталась
  // 1.5с после каждого удалённого события).
  const blockSyncFor = useCallback((ms: number) => {
    isInternalAction.current = true;
    if (internalActionTimer.current) {
      clearTimeout(internalActionTimer.current);
    }
    internalActionTimer.current = setTimeout(() => {
      isInternalAction.current = false;
      internalActionTimer.current = null;
    }, ms);
  }, []);

  // Постоянный id гостя: два гостя больше не схлопываются в одного на сервере (было «2 вместо 3»)
  const getGuestId = () => {
    try {
      let gid = localStorage.getItem('skycine_guest_id');
      if (!gid) {
        gid = `guest_${Math.random().toString(36).substring(2, 10)}`;
        localStorage.setItem('skycine_guest_id', gid);
      }
      return gid;
    } catch {
      return 'guest';
    }
  };

  useEffect(() => {
    if (room && user) {
      setIsHost(room.hostUserId === user.id);
    }
  }, [room, user]);

  const userRef = useRef(user);
  userRef.current = user;

  const onSeekToRef = useRef(onSeekTo);
  onSeekToRef.current = onSeekTo;

  const onPlayRef = useRef(onPlay);
  onPlayRef.current = onPlay;

  const onPauseRef = useRef(onPause);
  onPauseRef.current = onPause;

  const getCurrentTimeRef = useRef(getCurrentTime);
  getCurrentTimeRef.current = getCurrentTime;

  const getIsPausedRef = useRef(getIsPaused);
  getIsPausedRef.current = getIsPaused;

  const getRealPos = useCallback((): number => {
    if (getCurrentTimeRef.current) return getCurrentTimeRef.current();
    return videoRef?.current?.currentTime || 0;
  }, [videoRef]);

  const getRealPaused = useCallback((): boolean => {
    if (getIsPausedRef.current) return getIsPausedRef.current();
    if (videoRef?.current) return videoRef.current.paused;
    return roomStateRef.current !== 'PLAYING';
  }, [videoRef]);

  const executePlay = useCallback(() => {
    if (onPlayRef.current) {
      onPlayRef.current();
    } else if (videoRef?.current) {
      videoRef.current.play().catch(() => {});
    }
  }, [videoRef]);

  const executePause = useCallback(() => {
    if (onPauseRef.current) {
      onPauseRef.current();
    } else if (videoRef?.current) {
      videoRef.current.pause();
    }
  }, [videoRef]);

  const executeSeek = useCallback((pos: number, shouldPlay?: boolean) => {
    if (onSeekToRef.current) {
      onSeekToRef.current(pos, shouldPlay);
    } else if (videoRef?.current) {
      videoRef.current.currentTime = pos;
      if (shouldPlay) {
        videoRef.current.play().catch(() => {});
      }
    }
  }, [videoRef]);

  const hasInitializedRef = useRef(false);

  const isHostRef = useRef(isHost);
  isHostRef.current = isHost;

  const getSyncedServerTimeRef = useRef(getSyncedServerTime);
  getSyncedServerTimeRef.current = getSyncedServerTime;

  const getRttRef = useRef(getRtt);
  getRttRef.current = getRtt;

  const getHealthSnapshot = useCallback(() => {
    // -1 = неизвестно (нет video.buffered и MPV-статы ещё не пришли).
    // Ложный 0 давал всем "Мало буфера" ни за что.
    let bufferedAhead = mpvAheadSeenRef.current ? mpvAheadRef.current || 0 : -1;
    let bufferedEnd: number | undefined;
    let droppedWindow = 0;
    let paused = true;
    let readyState = 0;
    try {
      const v = videoRef?.current;
      if (v) {
        paused = v.paused;
        readyState = v.readyState || 0;
        const cur = v.currentTime || 0;
        if (v.buffered && v.buffered.length > 0) {
          try {
            bufferedEnd = v.buffered.end(v.buffered.length - 1);
            bufferedAhead = Math.max(0, bufferedEnd - cur);
          } catch {}
        } else {
          bufferedAhead = -1;
        }
        try {
          const q = (v as any).getVideoPlaybackQuality ? (v as any).getVideoPlaybackQuality() : null;
          if (q && typeof q.droppedVideoFrames === 'number') {
            const abs = q.droppedVideoFrames;
            droppedWindow = Math.max(0, abs - droppedBaseRef.current);
            droppedBaseRef.current = abs;
            droppedFramesRef.current = abs;
          }
        } catch {}
      } else {
        // MPV: пауза из колбэков плеера
        try { paused = getRealPaused(); } catch { paused = true; }
        droppedWindow = Math.max(0, droppedFramesRef.current - droppedBaseRef.current);
        droppedBaseRef.current = droppedFramesRef.current;
      }
    } catch {}
    const rawFlag = isBufferingRef.current;
    const effectiveBuffering = rawFlag && !paused && (videoRef?.current ? readyState < 3 : true);
    if (!effectiveBuffering && rawFlag && (!videoRef?.current || paused || readyState >= 3)) {
      isBufferingRef.current = false;
      stallStartRef.current = 0;
    }
    return {
      isBuffering: effectiveBuffering,
      isPlaying: !paused,
      bufferedAheadSec: bufferedAhead >= 0 ? Math.round(bufferedAhead * 10) / 10 : -1,
      bufferedEnd,
      stallCount: stallCountRef.current,
      stallMs: Math.round(stallMsRef.current),
      rttMs: Math.round(getRttRef.current ? getRttRef.current() : 0),
      droppedFrames: droppedWindow,
      platform: (typeof window !== 'undefined' && (window as any).desktopPlayer?.isDesktop) ? 'desktop' : 'web',
      hwdec: mpvHwdecRef.current,
    };
  }, [videoRef, getRealPaused]);

  // События <video> (YouTube/фолбэк). MPV пушит через reportDesktopHealth.
  // Пауза гасит флаг: seek на паузе даёт 'waiting' без 'playing' (залипание).
  useEffect(() => {
    const v = videoRef?.current;
    if (!v) return;
    const beginStall = () => {
      if (!isBufferingRef.current) {
        isBufferingRef.current = true;
        stallStartRef.current = Date.now();
        stallCountRef.current += 1;
      }
    };
    const endStall = () => {
      if (isBufferingRef.current) {
        isBufferingRef.current = false;
        if (stallStartRef.current) {
          stallMsRef.current += Date.now() - stallStartRef.current;
          stallStartRef.current = 0;
        }
      }
    };
    const onWaiting = () => beginStall();
    const onStalled = () => beginStall();
    const onPlaying = () => endStall();
    const onCanPlay = () => endStall();
    const onPause = () => endStall();
    const onSeeked = () => {
      try {
        if (v.paused || (v.readyState || 0) >= 3) endStall();
      } catch { endStall(); }
    };
    v.addEventListener('waiting', onWaiting);
    v.addEventListener('stalled', onStalled);
    v.addEventListener('playing', onPlaying);
    v.addEventListener('canplay', onCanPlay);
    v.addEventListener('pause', onPause);
    v.addEventListener('seeked', onSeeked);
    try { if (v.paused) endStall(); } catch {}
    return () => {
      v.removeEventListener('waiting', onWaiting);
      v.removeEventListener('stalled', onStalled);
      v.removeEventListener('playing', onPlaying);
      v.removeEventListener('canplay', onCanPlay);
      v.removeEventListener('pause', onPause);
      v.removeEventListener('seeked', onSeeked);
      endStall();
    };
  }, [videoRef, room?.id]);

  // ── Socket Events ──
  useEffect(() => {
    if (!socket || !room?.id) return;

    const targetRoomId = room.id;

    const joinRoom = () => {
      const currentUser = userRef.current;
      const uid = currentUser?.id || getGuestId();
      lastJoinedUserIdRef.current = uid;
      socket.emit('room:join', {
        roomId: targetRoomId,
        userId: uid,
        username: currentUser?.username || 'Гость',
        avatarUrl: currentUser?.avatarUrl,
        streamMode: 'direct',
      });
    };

    if (socket.connected) {
      joinRoom();
    }
    socket.on('connect', joinRoom);

    // Initial state on joining
    socket.on('room:initial_state', (data: { room: Room; members: RoomMember[]; serverTimestamp: number; livePosition: number }) => {
      setMembers(data.members || []);
      if (data.room) {
        roomStateRef.current = data.room.state;
        setRoomState(data.room.state);

        if (!hasInitializedRef.current) {
          hasInitializedRef.current = true;
          const livePos = data.livePosition || data.room.currentPosition || 0;
          const shouldPlay = data.room.state === 'PLAYING';

          blockSyncFor(2000);
          executeSeek(livePos, shouldPlay);
          if (shouldPlay) {
            executePlay();
          } else {
            executePause();
          }
        }
      }
    });

    socket.on('room:members', (updatedMembers: RoomMember[]) => {
      setMembers(updatedMembers || []);
    });

    // Synchronized state change (Play / Pause / Seek)
    socket.on('room:sync_state', (data: {
      state: RoomState;
      currentPosition: number;
      serverTimestamp: number;
      playbackRate: number;
      action: string;
      initiatedBy: string;
      initiatedByUserId?: string;
    }) => {
      roomStateRef.current = data.state;
      setRoomState(data.state);

      if (scheduledPlayTimer.current) {
        clearTimeout(scheduledPlayTimer.current);
        scheduledPlayTimer.current = null;
      }

      // Это эхо моего же действия — повторно не исполняем (иначе двойной seek и плавающая секунда)
      const now = Date.now();
      const isRecentLocalSeek =
        data.action === 'SEEK' &&
        lastSentSeekPosRef.current !== null &&
        Math.abs(data.currentPosition - lastSentSeekPosRef.current) < 1.5 &&
        (now - lastSentSeekTimeRef.current) < 5000;

      const isInitiator = Boolean(
        isRecentLocalSeek ||
        (data.initiatedByUserId &&
          userRef.current?.id &&
          data.initiatedByUserId === userRef.current.id)
      );

      if (data.action === 'PAUSE') {
        executePause();
        const cur = getRealPos();
        if (!isInitiator && Math.abs(cur - data.currentPosition) > 0.8) {
          executeSeek(data.currentPosition, false);
        }
        blockSyncFor(1500);
      } else if (data.action === 'PLAY') {
        const serverNow = getSyncedServerTimeRef.current();
        const delay = Math.max(0, data.serverTimestamp - serverNow);
        const cur = getRealPos();

        if (Math.abs(cur - data.currentPosition) > 1.5) {
          executeSeek(data.currentPosition, true);
        }

        blockSyncFor(1500);

        if (delay > 0) {
          scheduledPlayTimer.current = setTimeout(() => {
            executePlay();
          }, delay);
        } else {
          executePlay();
        }
      } else if (data.action === 'SEEK') {
        const shouldPlay = data.state === 'PLAYING';
        blockSyncFor(2500);

        // Инициатору свой же seek не повторяем
        if (!isInitiator) {
          executeSeek(data.currentPosition, shouldPlay);
        } else {
          lastSentSeekPosRef.current = null;
        }
        if (!shouldPlay) {
          executePause();
        }
      }
    });

    // Time Anchor от любого играющего (раньше только хост слал, остальные дрейфовали)
    socket.on('room:time_anchor', (data: { currentPosition: number; serverTimestamp: number }) => {
      if (isInternalAction.current) return;

      const now = getSyncedServerTimeRef.current();
      const elapsed = Math.max(0, (now - data.serverTimestamp) / 1000);
      const hostExpectedPos = data.currentPosition + (roomStateRef.current === 'PLAYING' ? elapsed : 0);
      const myPos = getRealPos();
      const diff = myPos - hostExpectedPos;

      setSyncDiffSec(Math.round(diff * 10) / 10);

      // Автокоррекция отключена: якорь только показывает дрейф, seek не делаем.
    });

    // Force Sync All from Host
    socket.on('room:force_sync_all', (data: { position: number; serverTimestamp: number; initiatedBy: string }) => {
      blockSyncFor(2500);
      const shouldPlay = roomStateRef.current === 'PLAYING';
      executeSeek(data.position, shouldPlay);
      if (shouldPlay) {
        executePlay();
      } else {
        executePause();
      }
      setSyncDiffSec(0);
    });

    // Chat and Reactions
    socket.on('room:chat_message', (msg: RoomChatMessage) => {
      setMessages((prev) => [...prev, msg]);
    });

    socket.on('room:reaction', (reaction: RoomReaction) => {
      setReactions((prev) => [...prev, reaction]);
      setTimeout(() => {
        setReactions((prev) => prev.filter((r) => r.id !== reaction.id));
      }, 3000);
    });

    socket.on('room:system_message', (sysMsg: { text: string; type: string; timestamp: number }) => {
      setMessages((prev) => [
        ...prev,
        {
          id: `sys-${Date.now()}-${Math.random()}`,
          userId: 'system',
          username: 'Система',
          text: sysMsg.text,
          timestamp: sysMsg.timestamp || Date.now(),
        },
      ]);
    });

    socket.on('room:health', (data: RoomHealthUpdate) => {
      if (!data) return;
      setHealth(data.health || []);
      setCulpritIds(data.culpritIds || []);
      setWaitingFor(data.waitingFor || []);
      setWaitingText(data.waitingText || null);
    });

    socket.on('room:action_feed', (entry: RoomActionFeedEntry) => {
      if (!entry) return;
      setActionFeed((prev) => [...prev.slice(-19), entry]);
      setTimeout(() => {
        setActionFeed((prev) => prev.filter((e) => e.id !== entry.id));
      }, 6000);
    });

    socket.on('room:rollback_notice', (notice: RoomRollbackNotice) => {
      if (!notice) return;
      setRollbackNotice(notice);
      setTimeout(() => {
        setRollbackNotice((prev) => (prev && prev.timestamp === notice.timestamp ? null : prev));
      }, 9000);
    });

    return () => {
      if (scheduledPlayTimer.current) clearTimeout(scheduledPlayTimer.current);
      if (internalActionTimer.current) clearTimeout(internalActionTimer.current);
      if (seekDebounceTimer.current) clearTimeout(seekDebounceTimer.current);
      socket.emit('room:leave', { roomId: targetRoomId });
      socket.off('connect', joinRoom);
      socket.off('room:initial_state');
      socket.off('room:members');
      socket.off('room:sync_state');
      socket.off('room:time_anchor');
      socket.off('room:force_sync_all');
      socket.off('room:chat_message');
      socket.off('room:reaction');
      socket.off('room:system_message');
      socket.off('room:health');
      socket.off('room:action_feed');
      socket.off('room:rollback_notice');
    };
  }, [socket, room?.id]);

  // Periodic Heartbeat: якорь шлёт КАЖДЫЙ играющий (раньше только хост — остальные дрейфовали)
  useEffect(() => {
    if (!socket || !room?.id || roomState !== 'PLAYING') return;

    const interval = setInterval(() => {
      if (!getRealPaused() && !isInternalAction.current) {
        const cur = getRealPos();
        socket.emit('room:host_heartbeat', {
          roomId: room.id,
          position: cur,
        });
      }
    }, 3000);

    return () => clearInterval(interval);
  }, [socket, room?.id, roomState, getRealPaused, getRealPos]);

  // Периодический репорт позиции + здоровья (каждые 3с). MPV всегда direct.
  // MPV-метрики (paused-for-cache и т.д.) приходят через reportDesktopHealth в mpvAheadRef/isBufferingRef.
  useEffect(() => {
    if (!socket || !room?.id) return;
    const sendStatus = () => {
      try {
        const snap = getHealthSnapshot();
        socket.emit('room:member_status', {
          roomId: room!.id,
          currentPosition: getRealPos(),
          bufferedPosition: snap.bufferedEnd,
          streamMode: 'direct',
          isBuffering: snap.isBuffering,
          isPlaying: snap.isPlaying,
          bufferedAheadSec: snap.bufferedAheadSec,
          stallCount: snap.stallCount,
          stallMs: snap.stallMs,
          rttMs: snap.rttMs,
          droppedFrames: snap.droppedFrames,
          platform: snap.platform,
          hwdec: snap.hwdec,
        });
        // Окно сталов отправлено — обнуляем (иначе накопление даст вечных "лагающих")
        stallCountRef.current = 0;
        stallMsRef.current = 0;
      } catch {}
    };
    sendStatus();
    const interval = setInterval(sendStatus, 3000);
    return () => clearInterval(interval);
  }, [socket, room?.id, getHealthSnapshot, getRealPos]);

  // Если юзер подтянулся ПОСЛЕ join (AuthContext грузится асинхронно) — пере-join с реальным id.
  // Иначе висим 'guest' и схлопываемся с другими гостями (было «2 вместо 3»).
  useEffect(() => {
    if (!socket || !socket.connected || !room?.id || !user?.id) return;
    if (lastJoinedUserIdRef.current === user.id) return;
    lastJoinedUserIdRef.current = user.id;
    socket.emit('room:join', {
      roomId: room.id,
      userId: user.id,
      username: user.username || 'Гость',
      avatarUrl: user.avatarUrl,
      streamMode: 'direct',
    });
  }, [socket, room?.id, user?.id, user?.username, user?.avatarUrl]);

  // ── Action Triggers (флаг isInternalAction исходящие НЕ дропает — иначе пауза/плей
  // молча глотались 1.5с после каждого удалённого события)
  const sendPlay = useCallback(() => {
    if (!socket || !room?.id) return;
    const cur = getRealPos();
    socket.emit('room:action', {
      roomId: room.id,
      action: 'PLAY',
      position: cur,
      userId: userRef.current?.id,
    });
  }, [socket, room?.id, getRealPos]);

  const sendPause = useCallback(() => {
    if (!socket || !room?.id) return;
    executePause();
    const cur = getRealPos();
    socket.emit('room:action', {
      roomId: room.id,
      action: 'PAUSE',
      position: cur,
      userId: userRef.current?.id,
    });
  }, [socket, room?.id, executePause, getRealPos]);

  const seekDebounceTimer = useRef<NodeJS.Timeout | null>(null);

  const sendSeek = useCallback((pos: number, shouldPlay?: boolean) => {
    if (!socket || !room?.id) return;
    const willPlay = shouldPlay !== undefined ? shouldPlay : !getRealPaused();
    blockSyncFor(2500);
    lastSentSeekPosRef.current = pos;
    lastSentSeekTimeRef.current = Date.now();
    executeSeek(pos, willPlay);

    if (seekDebounceTimer.current) {
      clearTimeout(seekDebounceTimer.current);
    }

    seekDebounceTimer.current = setTimeout(() => {
      lastSentSeekPosRef.current = pos;
      lastSentSeekTimeRef.current = Date.now();
      socket.emit('room:action', {
        roomId: room.id,
        action: 'SEEK',
        position: pos,
        shouldPlay: willPlay,
        userId: userRef.current?.id,
      });
      seekDebounceTimer.current = null;
      blockSyncFor(2500);
    }, 150);
  }, [socket, room?.id, executeSeek, getRealPaused, blockSyncFor]);

  const forceSyncAll = useCallback(() => {
    if (!socket || !room?.id) return;
    const cur = getRealPos();
    socket.emit('room:force_sync_all', {
      roomId: room.id,
      position: cur,
    });
    setSyncDiffSec(0);
  }, [socket, room?.id, getRealPos]);

  const syncToHost = useCallback(() => {
    if (!socket || !room?.id) return;
    const hostMember = members.find((m) => m.userId === room.hostUserId);
    if (hostMember && hostMember.currentPosition > 0) {
      blockSyncFor(2500);
      executeSeek(hostMember.currentPosition, roomStateRef.current === 'PLAYING');
      setSyncDiffSec(0);
    }
  }, [socket, room?.id, room?.hostUserId, members, executeSeek, blockSyncFor]);

  const sendMessage = useCallback((text: string) => {
    if (!socket || !room?.id || !text.trim()) return;
    const currentUser = userRef.current;
    socket.emit('room:chat_message', {
      roomId: room.id,
      text: text.trim(),
      userId: currentUser?.id,
      username: currentUser?.username,
      avatarUrl: currentUser?.avatarUrl,
    });
  }, [socket, room?.id]);

  const sendReaction = useCallback((emoji: string) => {
    if (!socket || !room?.id || !emoji) return;
    const currentUser = userRef.current;
    socket.emit('room:reaction', {
      roomId: room.id,
      emoji,
      username: currentUser?.username,
    });
  }, [socket, room?.id]);

  const sendFriendInvite = useCallback((targetUserId: string) => {
    if (!socket || !room) return;
    socket.emit('friend:invite_to_room', {
      targetUserId,
      roomId: room.id,
      roomCode: room.code,
      roomTitle: room.title,
      mediaTitle: room.mediaTitle || 'Фильм',
      posterPath: room.posterPath,
    });
  }, [socket, room]);

  // Внешний пуш MPV-телеметрии из CustomPlayer (mpv:buffering / mpv:stats).
  const reportDesktopHealth = useCallback((patch: {
    isBuffering?: boolean; bufferedAheadSec?: number; stallCount?: number;
    stallMs?: number; droppedFrames?: number; hwdec?: string;
  }) => {
    if (patch.isBuffering !== undefined) {
      if (patch.isBuffering && !isBufferingRef.current) {
        isBufferingRef.current = true;
        stallStartRef.current = Date.now();
        stallCountRef.current += 1;
      } else if (!patch.isBuffering && isBufferingRef.current) {
        isBufferingRef.current = false;
        if (stallStartRef.current) {
          stallMsRef.current += Date.now() - stallStartRef.current;
          stallStartRef.current = 0;
        }
      }
    }
    if (patch.bufferedAheadSec !== undefined && Number.isFinite(patch.bufferedAheadSec)) {
      mpvAheadRef.current = Math.max(0, patch.bufferedAheadSec);
      mpvAheadSeenRef.current = true;
    }
    if (patch.stallCount !== undefined) stallCountRef.current = patch.stallCount;
    if (patch.stallMs !== undefined) stallMsRef.current = patch.stallMs;
    if (patch.droppedFrames !== undefined) droppedFramesRef.current = patch.droppedFrames;
    if (patch.hwdec !== undefined) mpvHwdecRef.current = patch.hwdec;
    // Мгновенная отправка при смене буферизации (троттлинг 1с).
    // Честность как в снепшоте: paused-MPV не буферизуется.
    if (patch.isBuffering !== undefined && socket && room?.id) {
      const nowMs = Date.now();
      if (nowMs - lastBufferingEmitRef.current > 1000) {
        lastBufferingEmitRef.current = nowMs;
        try {
          const snap = getHealthSnapshot();
          socket.emit('room:member_status', {
            roomId: room.id,
            currentPosition: getRealPos(),
            streamMode: 'direct',
            isBuffering: snap.isBuffering,
            isPlaying: snap.isPlaying,
            bufferedAheadSec: snap.bufferedAheadSec,
            stallCount: snap.stallCount,
            stallMs: snap.stallMs,
            rttMs: snap.rttMs,
            droppedFrames: snap.droppedFrames,
            platform: 'desktop',
            hwdec: snap.hwdec,
          });
          stallCountRef.current = 0;
          stallMsRef.current = 0;
        } catch {}
      }
    }
  }, [socket, room?.id, getHealthSnapshot, getRealPos]);

  return {
    roomState,
    members,
    messages,
    reactions,
    syncDiffSec,
    isHost,
    health,
    culpritIds,
    waitingFor,
    waitingText,
    actionFeed,
    rollbackNotice,
    reportDesktopHealth,
    sendPlay,
    sendPause,
    sendSeek,
    forceSyncAll,
    syncToHost,
    sendMessage,
    sendReaction,
    sendFriendInvite,
  };
}
