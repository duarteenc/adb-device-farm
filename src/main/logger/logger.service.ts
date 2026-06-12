import type { LogEntry } from '@shared/types';
import { BrowserWindow } from 'electron';

export class LoggerService {
  private logs: LogEntry[] = [];
  private maxLogs: number = 1000;
  private mainWindow: BrowserWindow | null = null;

  setMainWindow(window: BrowserWindow): void {
    this.mainWindow = window;
  }

  log(level: LogEntry['level'], message: string, deviceSerial?: string, context?: string): void {
    const entry: LogEntry = {
      timestamp: Date.now(),
      level,
      message,
      deviceSerial,
      context,
    };

    this.logs.push(entry);

    // Keep only the last maxLogs entries
    if (this.logs.length > this.maxLogs) {
      this.logs = this.logs.slice(-this.maxLogs);
    }

    // Send log to renderer if window exists
    if (this.mainWindow && !this.mainWindow.isDestroyed()) {
      this.mainWindow.webContents.send('log:new', entry);
    }

    // Also log to console
    const prefix = deviceSerial ? `[${deviceSerial}]` : '';
    const contextStr = context ? `[${context}]` : '';
    const logMessage = `${contextStr}${prefix} ${message}`;

    switch (level) {
      case 'error':
        console.error(logMessage);
        break;
      case 'warn':
        console.warn(logMessage);
        break;
      case 'success':
      case 'info':
      default:
        console.log(logMessage);
        break;
    }
  }

  info(message: string, deviceSerial?: string, context?: string): void {
    this.log('info', message, deviceSerial, context);
  }

  success(message: string, deviceSerial?: string, context?: string): void {
    this.log('success', message, deviceSerial, context);
  }

  warn(message: string, deviceSerial?: string, context?: string): void {
    this.log('warn', message, deviceSerial, context);
  }

  error(message: string, deviceSerial?: string, context?: string): void {
    this.log('error', message, deviceSerial, context);
  }

  getLogs(limit?: number): LogEntry[] {
    if (limit) {
      return this.logs.slice(-limit);
    }
    return [...this.logs];
  }

  clearLogs(): void {
    this.logs = [];
    if (this.mainWindow && !this.mainWindow.isDestroyed()) {
      this.mainWindow.webContents.send('log:clear');
    }
  }

  getLogsByDevice(deviceSerial: string): LogEntry[] {
    return this.logs.filter((log) => log.deviceSerial === deviceSerial);
  }

  getLogsByLevel(level: LogEntry['level']): LogEntry[] {
    return this.logs.filter((log) => log.level === level);
  }
}

export const logger = new LoggerService();
