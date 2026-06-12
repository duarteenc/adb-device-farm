export type DeviceStatus = 'online' | 'offline' | 'unauthorized' | 'disconnected' | 'connecting';

export interface Device {
  serial: string;
  status: DeviceStatus;
  ip?: string;
  model?: string;
  androidVersion?: string;
  battery?: number;
  resolution?: string;
  manufacturer?: string;
  deviceName?: string;
  lastSeen?: number;
  connectionType: 'usb' | 'wifi';
}

export interface DeviceInfo {
  model: string;
  androidVersion: string;
  manufacturer: string;
  deviceName: string;
  resolution: string;
  battery: number;
}

export interface ADBCommand {
  command: string;
  args: string[];
  timeout?: number;
}

export interface ADBResult {
  success: boolean;
  output: string;
  error?: string;
}

export interface LogEntry {
  timestamp: number;
  level: 'info' | 'warn' | 'error' | 'success';
  message: string;
  deviceSerial?: string;
  context?: string;
}

export interface MacroAction {
  type: 'tap' | 'swipe' | 'text' | 'keyevent' | 'shell' | 'delay' | 'app_start' | 'app_stop';
  params: Record<string, unknown>;
  description?: string;
}

export interface Macro {
  id: string;
  name: string;
  description?: string;
  actions: MacroAction[];
  createdAt: number;
  updatedAt: number;
}

export interface MacroExecution {
  macroId: string;
  deviceSerials: string[];
  status: 'pending' | 'running' | 'completed' | 'failed' | 'cancelled';
  startTime?: number;
  endTime?: number;
  results?: Map<string, MacroExecutionResult>;
}

export interface MacroExecutionResult {
  deviceSerial: string;
  success: boolean;
  errors: string[];
  completedActions: number;
  totalActions: number;
}
