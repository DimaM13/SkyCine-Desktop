import axios from 'axios';

export function getServerUrl(): string {
  if (typeof window === 'undefined') return '';
  let custom = localStorage.getItem('skycine_server_url');
  if (custom) {
    custom = custom.trim().replace(/\/+$/, '');
    if (custom.endsWith(':3000')) {
      custom = custom.replace(/:3000$/, ':5000');
    }
    return custom;
  }
  
  if (window.location.protocol === 'http:' || window.location.protocol === 'https:') {
    if (window.location.port === '3000') {
      return `${window.location.protocol}//${window.location.hostname}:5000`;
    }
    return window.location.origin;
  }
  return '';
}

export function setServerUrl(url: string): void {
  let cleanUrl = url.trim().replace(/\/+$/, '');
  if (cleanUrl.endsWith(':3000')) {
    cleanUrl = cleanUrl.replace(/:3000$/, ':5000');
  }
  localStorage.setItem('skycine_server_url', cleanUrl);
}

export function resolveMediaUrl(path?: string | null): string {
  if (!path) return '';
  if (path.startsWith('http://') || path.startsWith('https://') || path.startsWith('data:') || path.startsWith('blob:')) {
    return path;
  }
  const base = getServerUrl();
  const cleanPath = path.startsWith('/') ? path : `/${path}`;
  return base ? `${base}${cleanPath}` : cleanPath;
}

export const API_BASE_URL = `${getServerUrl()}/api`;

export const apiClient = axios.create({
  baseURL: API_BASE_URL,
  headers: {
    'Content-Type': 'application/json',
  },
});

apiClient.interceptors.request.use((config) => {
  config.baseURL = `${getServerUrl()}/api`;
  const token = localStorage.getItem('myplex_token');
  if (token) {
    config.headers.Authorization = `Bearer ${token}`;
  }
  return config;
});

apiClient.interceptors.response.use(
  (response) => response,
  (error) => {
    // Only clear stored token if the authentication check itself (/auth/me) explicitly fails
    if (error.config?.url?.includes('/auth/me') && error.response?.status === 401) {
      localStorage.removeItem('myplex_token');
      localStorage.removeItem('myplex_user');
    }
    return Promise.reject(error);
  }
);
