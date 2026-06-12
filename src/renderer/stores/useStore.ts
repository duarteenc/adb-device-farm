import { create } from 'zustand';
import type { Device, LogEntry } from '@shared/types';

interface AppState {
  devices: Device[];
  selectedDevices: Set<string>;
  logs: LogEntry[];
  isLoading: boolean;
  error: string | null;
  activeLiveViews: Set<string>;
  scrcpyAvailable: boolean;

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
  setActiveLiveViews: (views: string[]) => void;
  addActiveLiveView: (serial: string) => void;
  removeActiveLiveView: (serial: string) => void;
  setScrcpyAvailable: (available: boolean) => void;
}

export const useStore = create<AppState>((set, get) => ({
  devices: [],
  selectedDevices: new Set(),
  logs: [],
  isLoading: false,
  error: null,
  activeLiveViews: new Set(),
  scrcpyAvailable: false,

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

  setActiveLiveViews: (views) => set({ activeLiveViews: new Set(views) }),

  addActiveLiveView: (serial) =>
    set((state) => ({
      activeLiveViews: new Set([...state.activeLiveViews, serial]),
    })),

  removeActiveLiveView: (serial) =>
    set((state) => {
      const newSet = new Set(state.activeLiveViews);
      newSet.delete(serial);
      return { activeLiveViews: newSet };
    }),

  setScrcpyAvailable: (available) => set({ scrcpyAvailable: available }),
}));
