import React, { useState } from 'react';
import { Link, useLocation, useNavigate } from 'react-router-dom';
import {
  Users, Shield, LogOut, User as UserIcon,
  Play, Menu, Radio, Server
} from 'lucide-react';
import { useAuth } from '../../context/AuthContext';
import { ServerSettingsModal } from '../auth/ServerSettingsModal';

interface NavbarProps {
  onToggleSidebar?: () => void;
}

export const Navbar: React.FC<NavbarProps> = ({ onToggleSidebar }) => {
  const { user, logout, isAdmin } = useAuth();
  const location = useLocation();
  const navigate = useNavigate();
  const [showUserMenu, setShowUserMenu] = useState(false);
  const [showServerModal, setShowServerModal] = useState(false);
  const isDesktop = typeof window !== 'undefined' && Boolean((window as any).desktopPlayer?.isDesktop);

  const isPlayerScreen =
    location.pathname.startsWith('/watch') ||
    (/^\/rooms\/[a-zA-Z0-9_-]+$/.test(location.pathname) && location.pathname !== '/rooms');

  if (isPlayerScreen) {
    return null;
  }

  const isActive = (path: string) => location.pathname === path;

  const navLinks = [
    { path: '/rooms', label: 'Комнаты', icon: Radio },
    { path: '/friends', label: 'Друзья', icon: Users },
  ];

  if (isAdmin) {
    navLinks.push({ path: '/admin', label: 'Панель сервера', icon: Shield });
  }

  const handleLogout = () => {
    logout();
    navigate('/auth');
  };

  return (
    <header className="sticky top-0 z-40 bg-cinema-950/80 backdrop-blur-xl border-b border-white/10 h-20">
      <div className="w-full px-4 md:px-8 h-full flex items-center justify-between">
        {/* Left: Mobile Toggle & Brand Logo */}
        <div className="flex items-center gap-3">
          {user && (
            <button
              onClick={onToggleSidebar}
              className="md:hidden p-2 rounded-xl bg-white/5 border border-white/10 text-white hover:bg-white/10 transition-colors"
              title="Открыть меню библиотек"
            >
              <Menu className="w-5 h-5" />
            </button>
          )}

          <Link to="/" className="flex items-center gap-3 group">
            <div className="w-10 h-10 rounded-2xl bg-gradient-to-tr from-cinema-gold to-yellow-300 flex items-center justify-center shadow-glow-gold transform group-hover:scale-105 transition-transform">
              <Play className="w-5 h-5 text-black fill-black ml-0.5" />
            </div>
            <div className="flex flex-col">
              <span className="text-xl font-extrabold text-white tracking-wider flex items-center gap-1 font-['Outfit']">
                Sky<span className="text-cinema-gold">Cine</span>
              </span>
              <span className="text-[10px] text-cinema-gold font-semibold uppercase tracking-widest -mt-1 hidden sm:block">
                Personal Cinema & Sync
              </span>
            </div>
          </Link>
        </div>

        {/* Right Nav & User Area */}
        <div className="flex items-center gap-3 md:gap-4">
          {/* Desktop Navigation */}
          {user && (
            <nav className="hidden md:flex items-center gap-1 bg-white/5 border border-white/5 p-1 rounded-2xl">
              {navLinks.map((link) => {
                const Icon = link.icon;
                const active = isActive(link.path);
                return (
                  <Link
                    key={link.path}
                    to={link.path}
                    className={`px-4 py-2 rounded-xl text-xs font-semibold flex items-center gap-2 transition-all ${
                      active
                        ? 'bg-cinema-gold text-black shadow-glow-gold'
                        : 'text-slate-300 hover:text-white hover:bg-white/5'
                    }`}
                  >
                    <Icon className="w-4 h-4" />
                    <span>{link.label}</span>
                  </Link>
                );
              })}
            </nav>
          )}

          {/* User Profile / Auth Area */}
          {isDesktop && (
            <button
              onClick={() => setShowServerModal(true)}
              className="p-2.5 rounded-2xl bg-white/5 border border-white/10 hover:bg-white/10 text-slate-300 hover:text-cinema-gold transition-colors flex items-center gap-1.5 text-xs font-semibold"
              title="Настройки сервера"
            >
              <Server className="w-4 h-4 text-cinema-gold" />
              <span className="hidden sm:inline">Сервер</span>
            </button>
          )}

          {user ? (
            <div className="relative">
              <button
                onClick={() => setShowUserMenu(!showUserMenu)}
                className="flex items-center gap-3 p-1.5 pr-3 rounded-2xl bg-white/5 border border-white/10 hover:bg-white/10 transition-colors"
              >
                <div className="w-8 h-8 rounded-xl bg-gradient-to-tr from-cinema-gold to-yellow-300 flex items-center justify-center font-bold text-black text-xs shadow-md">
                  {user.avatarUrl ? (
                    <img src={user.avatarUrl} alt={user.username} className="w-full h-full object-cover rounded-xl" />
                  ) : (
                    user.username.charAt(0).toUpperCase()
                  )}
                </div>
                <span className="text-xs font-bold text-white hidden sm:inline max-w-[120px] truncate">
                  {user.username}
                </span>
              </button>

              {/* User Dropdown Menu */}
              {showUserMenu && (
                <div className="absolute right-0 mt-2 w-48 bg-cinema-900 border border-white/15 rounded-2xl p-2 shadow-2xl z-50 animate-fade-in flex flex-col gap-1 text-xs">
                  <Link
                    to="/friends"
                    onClick={() => setShowUserMenu(false)}
                    className="p-2.5 rounded-xl hover:bg-white/10 text-slate-300 hover:text-white flex items-center gap-2 transition-colors"
                  >
                    <Users className="w-4 h-4 text-cinema-gold" />
                    <span>Друзья и заявки</span>
                  </Link>

                  {isAdmin && (
                    <Link
                      to="/admin"
                      onClick={() => setShowUserMenu(false)}
                      className="p-2.5 rounded-xl hover:bg-white/10 text-slate-300 hover:text-white flex items-center gap-2 transition-colors"
                    >
                      <Shield className="w-4 h-4 text-cinema-gold" />
                      <span>Панель сервера</span>
                    </Link>
                  )}

                  <div className="my-1 border-t border-white/10" />

                  <button
                    onClick={handleLogout}
                    className="p-2.5 rounded-xl hover:bg-red-500/20 text-red-400 flex items-center gap-2 transition-colors w-full text-left"
                  >
                    <LogOut className="w-4 h-4" />
                    <span>Выйти</span>
                  </button>
                </div>
              )}
            </div>
          ) : (
            <Link
              to="/auth"
              className="px-5 py-2.5 rounded-2xl bg-cinema-gold text-black font-bold text-xs shadow-glow-gold hover:bg-yellow-400 transition-all"
            >
              Войти / Регистрация
            </Link>
          )}
        </div>
      </div>

      <ServerSettingsModal
        isOpen={showServerModal}
        onClose={() => setShowServerModal(false)}
      />
    </header>
  );
};
