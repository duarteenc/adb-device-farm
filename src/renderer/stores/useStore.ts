import { create } from 'zustand';
import type { Device, LogEntry } from '@shared/types';

interface AppState {
  devices: Device[];
  selectedDevices: Set<string>;
  logs: LogEntry[];
  isLoading: boolean;
  error: string | null;

  // Actions
  setDevices: (devices: Device[]) => void;
  selectDevice: (serial: string) => void;
  deselectDevice: (serial: string) => void;
  toggleDeviceSelection: (serial: string) => void;
  clearSelection: () => void;
  selectAll: () => void;
  addLog: (log: LogEntry) => void;
  setLogs: (logs: LogEntry[]) => void;
  clearLogs: () => void;
  setLoading: (loading: boolean) => void;
  setError: (error: string | null) => void;
}

export const useStore = create<AppState>((set, get) => ({
  devices: [],
  selectedDevices: new Set(),
  logs: [],
  isLoading: false,
  error: null,

  setDevices: (devices) => set({ devices }),

  selectDevice: (serial) =>
    set((state) => ({
      selectedDevices: new Set([...state.selectedDevices, serial]),
    })),

  deselectDevice: (serial) =>
    set((state) => {
      const newSet = new Set(state.selectedDevices);
      newSet.delete(serial);
      return { selectedDevices: newSet };
    }),

  toggleDeviceSelection: (serial) => {
    const state = get();
    if (state.selectedDevices.has(serial)) {
      state.deselectDevice(serial);
    } else {
      state.selectDevice(serial);
    }
  },

  clearSelection: () => set({ selectedDevices: new Set() }),

  selectAll: () =>
    set((state) => ({
      selectedDevices: new Set(state.devices.map((d) => d.serial)),
    })),

  addLog: (log) =>
    set((state) => ({
      logs: [...state.logs, log].slice(-1000), // Keep last 1000 logs
    })),

  setLogs: (logs) => set({ logs }),

  clearLogs: () => set({ logs: [] }),

  setLoading: (loading) => set({ isLoading: loading }),

  setError: (error) => set({ error }),
}));
