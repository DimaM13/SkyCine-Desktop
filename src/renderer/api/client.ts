import axios from 'axios';

export function getServerUrl(): string {
  if (typeof window === 'undefined') return 'http://109.104.188.66:5000';
  const custom = localStorage.getItem('skycine_server_url');
  if (custom) return custom.replace(/\/+$/, '');
  
  if (window.location.protocol === 'http:' || window.location.protocol === 'https:') {
    if (window.location.port === '3000') {
      return `${window.location.protocol}//${window.location.hostname}:5000`;
    }
    return window.location.origin;
  }
  return 'http://109.104.188.66:5000';
}

export function setServerUrl(url: string): void {
  const cleanUrl = url.trim().replace(/\/+$/, '');
  localStorage.setItem('skycine_server_url', cleanUrl);
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
