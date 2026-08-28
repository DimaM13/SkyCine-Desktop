import React, { useState } from 'react';
import { Server, Check, AlertCircle, RefreshCw, X } from 'lucide-react';
import axios from 'axios';
import { getServerUrl, setServerUrl } from '../../api/client';

interface ServerSettingsModalProps {
  isOpen: boolean;
  onClose?: () => void;
  isInitialSetup?: boolean;
}

export const ServerSettingsModal: React.FC<ServerSettingsModalProps> = ({
  isOpen,
  onClose,
  isInitialSetup = false,
}) => {
  const [url, setUrl] = useState(getServerUrl() || 'http://');
  const [testing, setTesting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState(false);

  if (!isOpen) return null;

  const handleSave = async (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);
    setSuccess(false);

    let cleanUrl = url.trim().replace(/\/+$/, '');
    if (!cleanUrl.startsWith('http://') && !cleanUrl.startsWith('https://')) {
      cleanUrl = `http://${cleanUrl}`;
    }

    setTesting(true);
    try {
      // Test server connectivity
      await axios.get(`${cleanUrl}/api/auth/me`, { timeout: 5000 });
      setServerUrl(cleanUrl);
      setSuccess(true);
      setTimeout(() => {
        onClose?.();
        window.location.hash = '#/auth';
      }, 500);
    } catch (err: any) {
      // Even if 401 Unauthorized, server responded and is alive!
      if (err.response?.status === 401) {
        setServerUrl(cleanUrl);
        setSuccess(true);
        setTimeout(() => {
          onClose?.();
          window.location.hash = '#/auth';
        }, 500);
      } else {
        setError('Не удалось подключиться к серверу. Проверьте адрес, порт и включен ли сервер.');
      }
    } finally {
      setTesting(false);
    }
  };

  return (
    <div className="fixed inset-0 z-50 bg-black/80 backdrop-blur-md flex items-center justify-center p-4">
      <div className="w-full max-w-md bg-cinema-900 border border-cinema-gold/30 rounded-3xl p-6 shadow-2xl relative">
        {!isInitialSetup && onClose && (
          <button
            onClick={onClose}
            className="absolute top-4 right-4 p-2 text-slate-400 hover:text-white rounded-xl hover:bg-white/10"
          >
            <X className="w-5 h-5" />
          </button>
        )}

        <div className="flex items-center gap-3 mb-4">
          <div className="p-3 bg-cinema-gold/15 text-cinema-gold rounded-2xl border border-cinema-gold/20">
            <Server className="w-6 h-6" />
          </div>
          <div>
            <h2 className="text-lg font-bold text-white font-['Outfit']">
              {isInitialSetup ? 'Подключение к серверу SkyCine' : 'Настройки сервера'}
            </h2>
            <p className="text-xs text-slate-400">
              Укажите адрес вашего сервера SkyCine
            </p>
          </div>
        </div>

        <form onSubmit={handleSave} className="space-y-4">
          <div>
            <label className="block text-xs font-semibold text-slate-300 mb-1.5">
              URL адрес сервера:
            </label>
            <input
              type="text"
              value={url}
              onChange={(e) => setUrl(e.target.value)}
              placeholder="http://192.168.1.100:5000"
              className="w-full bg-cinema-950/80 border border-white/15 rounded-xl px-4 py-3 text-sm text-white focus:border-cinema-gold outline-none font-mono"
              required
            />
            <span className="text-[11px] text-slate-500 mt-1 block">
              Пример: <code className="text-slate-400">http://192.168.0.100:5000</code> или домен
            </span>
          </div>

          {error && (
            <div className="flex items-center gap-2 p-3 bg-red-500/10 border border-red-500/20 text-red-400 rounded-xl text-xs">
              <AlertCircle className="w-4 h-4 shrink-0" />
              <span>{error}</span>
            </div>
          )}

          {success && (
            <div className="flex items-center gap-2 p-3 bg-emerald-500/10 border border-emerald-500/20 text-emerald-400 rounded-xl text-xs">
              <Check className="w-4 h-4 shrink-0" />
              <span>Подключение успешно! Перезагрузка...</span>
            </div>
          )}

          <button
            type="submit"
            disabled={testing}
            className="w-full py-3 bg-gradient-to-r from-cinema-gold to-yellow-500 hover:from-yellow-400 hover:to-cinema-gold text-black font-bold rounded-xl transition-all shadow-lg hover:shadow-cinema-gold/30 flex items-center justify-center gap-2 cursor-pointer disabled:opacity-50"
          >
            {testing ? (
              <>
                <RefreshCw className="w-4 h-4 animate-spin" />
                <span>Проверка соединения...</span>
              </>
            ) : (
              <span>Сохранить и подключиться</span>
            )}
          </button>
        </form>
      </div>
    </div>
  );
};
