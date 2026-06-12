import { exec } from 'child_process';
import { promisify } from 'util';
import type { ADBCommand, ADBResult } from '@shared/types';

const execAsync = promisify(exec);

export class ADBService {
  private adbPath: string = 'C:\\platform-tools\\adb.exe';

  constructor() {
    this.detectADB();
  }

  private async detectADB(): Promise<void> {
    try {
      await this.executeCommand({ command: 'version', args: [] });
      console.log('[ADB] ADB detected successfully');
    } catch {
      console.error('[ADB] ADB not found in PATH. Please install Android Platform Tools.');
    }
  }

  async executeCommand(cmd: ADBCommand): Promise<ADBResult> {
    const fullCommand = `${this.adbPath} ${cmd.command} ${cmd.args.join(' ')}`.trim();

    try {
      const { stdout, stderr } = await execAsync(fullCommand, {
        timeout: cmd.timeout || 30000,
        maxBuffer: 1024 * 1024 * 10, // 10MB buffer
      });

      return {
        success: true,
        output: stdout.trim(),
        error: stderr.trim() || undefined,
      };
    } catch (error) {
      return {
        success: false,
        output: '',
        error: error instanceof Error ? error.message : 'Unknown error',
      };
    }
  }

  async executeDeviceCommand(serial: string, cmd: string, args: string[] = []): Promise<ADBResult> {
    return this.executeCommand({
      command: `-s ${serial} ${cmd}`,
      args,
    });
  }

  async getDevices(): Promise<string[]> {
    const result = await this.executeCommand({ command: 'devices', args: [] });

    if (!result.success) {
      throw new Error(`Failed to get devices: ${result.error}`);
    }

    const lines = result.output.split('\n').slice(1);
    const devices: string[] = [];

    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed) continue;

      const [serial, status] = trimmed.split('\t');
      if (serial && status) {
        devices.push(serial);
      }
    }

    return devices;
  }

  async getDeviceStatus(serial: string): Promise<string> {
    const result = await this.executeCommand({ command: 'devices', args: [] });

    if (!result.success) {
      return 'disconnected';
    }

    const lines = result.output.split('\n').slice(1);

    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed) continue;

      const [deviceSerial, status] = trimmed.split('\t');
      if (deviceSerial === serial) {
        return status || 'unknown';
      }
    }

    return 'disconnected';
  }

  async connectDevice(ip: string, port: number = 5555): Promise<ADBResult> {
    return this.executeCommand({
      command: 'connect',
      args: [`${ip}:${port}`],
    });
  }

  async disconnectDevice(ip: string, port: number = 5555): Promise<ADBResult> {
    return this.executeCommand({
      command: 'disconnect',
      args: [`${ip}:${port}`],
    });
  }

  async getDeviceProperty(serial: string, property: string): Promise<string> {
    const result = await this.executeDeviceCommand(serial, 'shell', [`getprop ${property}`]);
    return result.success ? result.output.trim() : '';
  }

  async getBatteryLevel(serial: string): Promise<number> {
    const result = await this.executeDeviceCommand(serial, 'shell', ['dumpsys battery | grep level']);

    if (!result.success) return -1;

    const match = result.output.match(/level: (\d+)/);
    return match ? parseInt(match[1], 10) : -1;
  }

  async tapScreen(serial: string, x: number, y: number): Promise<ADBResult> {
    return this.executeDeviceCommand(serial, 'shell', [`input tap ${x} ${y}`]);
  }

  async swipe(serial: string, x1: number, y1: number, x2: number, y2: number, duration: number = 300): Promise<ADBResult> {
    return this.executeDeviceCommand(serial, 'shell', [`input swipe ${x1} ${y1} ${x2} ${y2} ${duration}`]);
  }

  async inputText(serial: string, text: string): Promise<ADBResult> {
    const escapedText = text.replace(/\s/g, '%s').replace(/[&|;<>()$`\\"\n]/g, '\\$&');
    return this.executeDeviceCommand(serial, 'shell', [`input text ${escapedText}`]);
  }

  async pressKey(serial: string, keycode: string | number): Promise<ADBResult> {
    return this.executeDeviceCommand(serial, 'shell', [`input keyevent ${keycode}`]);
  }

  async takeScreenshot(serial: string, outputPath: string): Promise<ADBResult> {
    const devicePath = '/sdcard/screenshot.png';

    const captureResult = await this.executeDeviceCommand(serial, 'shell', [`screencap -p ${devicePath}`]);
    if (!captureResult.success) return captureResult;

    return this.executeDeviceCommand(serial, 'pull', [devicePath, outputPath]);
  }

  async installApp(serial: string, apkPath: string): Promise<ADBResult> {
    return this.executeDeviceCommand(serial, 'install', ['-r', apkPath]);
  }

  async uninstallApp(serial: string, packageName: string): Promise<ADBResult> {
    return this.executeDeviceCommand(serial, 'uninstall', [packageName]);
  }

  async startApp(serial: string, packageName: string): Promise<ADBResult> {
    return this.executeDeviceCommand(serial, 'shell', [`monkey -p ${packageName} 1`]);
  }

  async stopApp(serial: string, packageName: string): Promise<ADBResult> {
    return this.executeDeviceCommand(serial, 'shell', [`am force-stop ${packageName}`]);
  }

  async clearAppData(serial: string, packageName: string): Promise<ADBResult> {
    return this.executeDeviceCommand(serial, 'shell', [`pm clear ${packageName}`]);
  }

  async rebootDevice(serial: string): Promise<ADBResult> {
    return this.executeDeviceCommand(serial, 'reboot', []);
  }
}

export const adbService = new ADBService();
