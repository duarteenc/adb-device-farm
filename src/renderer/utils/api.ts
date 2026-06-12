import { ipcRenderer } from 'electron';
import type { Device, ADBResult, LogEntry } from '@shared/types';

export const api = {
  // Device Management
  devices: {
    discover: (): Promise<Device[]> => ipcRenderer.invoke('devices:discover'),
    getAll: (): Promise<Device[]> => ipcRenderer.invoke('devices:getAll'),
    connect: (ip: string, port?: number): Promise<boolean> => ipcRenderer.invoke('devices:connect', ip, port),
    disconnect: (ip: string, port?: number): Promise<boolean> => ipcRenderer.invoke('devices:disconnect', ip, port),
  },

  // ADB Commands
  adb: {
    tap: (serial: string, x: number, y: number): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:tap', serial, x, y),

    swipe: (serial: string, x1: number, y1: number, x2: number, y2: number, duration?: number): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:swipe', serial, x1, y1, x2, y2, duration),

    inputText: (serial: string, text: string): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:inputText', serial, text),

    pressKey: (serial: string, keycode: string | number): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:pressKey', serial, keycode),

    screenshot: (serial: string, outputPath: string): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:screenshot', serial, outputPath),

    install: (serial: string, apkPath: string): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:install', serial, apkPath),

    uninstall: (serial: string, packageName: string): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:uninstall', serial, packageName),

    startApp: (serial: string, packageName: string): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:startApp', serial, packageName),

    stopApp: (serial: string, packageName: string): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:stopApp', serial, packageName),

    clearAppData: (serial: string, packageName: string): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:clearAppData', serial, packageName),

    reboot: (serial: string): Promise<ADBResult> =>
      ipcRenderer.invoke('adb:reboot', serial),
  },

  // Logger
  logs: {
    getAll: (limit?: number): Promise<LogEntry[]> => ipcRenderer.invoke('logs:getAll', limit),
    clear: (): Promise<void> => ipcRenderer.invoke('logs:clear'),
    getByDevice: (serial: string): Promise<LogEntry[]> => ipcRenderer.invoke('logs:getByDevice', serial),
    onNewLog: (callback: (log: LogEntry) => void) => {
      ipcRenderer.on('log:new', (_, log) => callback(log));
    },
    onClearLogs: (callback: () => void) => {
      ipcRenderer.on('log:clear', () => callback());
    },
  },

  // Scrcpy
  scrcpy: {
    isAvailable: (): Promise<boolean> => ipcRenderer.invoke('scrcpy:isAvailable'),
    openLiveView: (serial: string, options?: {
      maxSize?: number;
      maxFps?: number;
      bitRate?: string;
    }): Promise<boolean> => ipcRenderer.invoke('scrcpy:openLiveView', serial, options),
    closeLiveView: (serial: string): Promise<boolean> => ipcRenderer.invoke('scrcpy:closeLiveView', serial),
    isDeviceActive: (serial: string): Promise<boolean> => ipcRenderer.invoke('scrcpy:isDeviceActive', serial),
    getActiveDevices: (): Promise<string[]> => ipcRenderer.invoke('scrcpy:getActiveDevices'),
    getActiveCount: (): Promise<number> => ipcRenderer.invoke('scrcpy:getActiveCount'),
  },

  // Screenshot
  screenshot: {
    capture: (serial: string): Promise<string | null> => ipcRenderer.invoke('screenshot:capture', serial),
    getCached: (serial: string): Promise<string | null> => ipcRenderer.invoke('screenshot:getCached', serial),
    startAutoCapture: (serial: string, intervalMs?: number): Promise<void> => ipcRenderer.invoke('screenshot:startAutoCapture', serial, intervalMs),
    stopAutoCapture: (serial: string): Promise<void> => ipcRenderer.invoke('screenshot:stopAutoCapture', serial),
    getActiveCaptures: (): Promise<string[]> => ipcRenderer.invoke('screenshot:getActiveCaptures'),
  },
};
