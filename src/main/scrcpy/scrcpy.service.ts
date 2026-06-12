import { spawn, ChildProcess } from 'child_process';
import { exec } from 'child_process';
import { promisify } from 'util';
import { logger } from '@main/logger/logger.service';

const execAsync = promisify(exec);

interface ScrcpyProcess {
  serial: string;
  process: ChildProcess;
  startTime: number;
}

export class ScrcpyService {
  private scrcpyPath: string = 'C:\\scrcpy\\scrcpy-win64-v2.7\\scrcpy.exe';
  private activeProcesses: Map<string, ScrcpyProcess> = new Map();
  private scrcpyAvailable: boolean = false;

  constructor() {
    this.detectScrcpy();
  }

  private async detectScrcpy(): Promise<void> {
    try {
      await execAsync(`"${this.scrcpyPath}" --version`);
      this.scrcpyAvailable = true;
      logger.info('scrcpy detected successfully', undefined, 'Scrcpy');
    } catch {
      logger.warn('scrcpy not found. Install from: https://github.com/Genymobile/scrcpy', undefined, 'Scrcpy');
      this.scrcpyAvailable = false;
    }
  }

  isAvailable(): boolean {
    return this.scrcpyAvailable;
  }

  isDeviceActive(serial: string): boolean {
    return this.activeProcesses.has(serial);
  }

  async openLiveView(serial: string, options?: {
    maxSize?: number;
    maxFps?: number;
    bitRate?: string;
  }): Promise<boolean> {
    if (!this.scrcpyAvailable) {
      logger.error('scrcpy is not available', serial, 'Scrcpy');
      return false;
    }

    // Avoid duplicate sessions
    if (this.activeProcesses.has(serial)) {
      logger.warn('Live view already active for this device', serial, 'Scrcpy');
      return false;
    }

    try {
      const args = [
        '--serial', serial,
        '--window-title', `ADB Device Farm - ${serial}`,
      ];

      // Add optional parameters
      if (options?.maxSize) {
        args.push('--max-size', options.maxSize.toString());
      }
      if (options?.maxFps) {
        args.push('--max-fps', options.maxFps.toString());
      }
      if (options?.bitRate) {
        args.push('--video-bit-rate', options.bitRate);
      }

      logger.info(`Opening live view with scrcpy`, serial, 'Scrcpy');

      const process = spawn(this.scrcpyPath, args, {
        detached: false,
        stdio: ['ignore', 'pipe', 'pipe'],
      });

      // Store process reference
      this.activeProcesses.set(serial, {
        serial,
        process,
        startTime: Date.now(),
      });

      // Handle process output
      process.stdout?.on('data', (data) => {
        logger.info(`scrcpy: ${data.toString().trim()}`, serial, 'Scrcpy');
      });

      process.stderr?.on('data', (data) => {
        const message = data.toString().trim();
        if (message.includes('ERROR') || message.includes('WARN')) {
          logger.warn(`scrcpy: ${message}`, serial, 'Scrcpy');
        }
      });

      // Handle process exit
      process.on('exit', (code) => {
        this.activeProcesses.delete(serial);
        if (code === 0) {
          logger.info('Live view closed normally', serial, 'Scrcpy');
        } else {
          logger.warn(`Live view exited with code ${code}`, serial, 'Scrcpy');
        }
      });

      process.on('error', (error) => {
        this.activeProcesses.delete(serial);
        logger.error(`Failed to start scrcpy: ${error.message}`, serial, 'Scrcpy');
      });

      logger.success('Live view opened successfully', serial, 'Scrcpy');
      return true;
    } catch (error) {
      logger.error(`Failed to open live view: ${error instanceof Error ? error.message : 'Unknown error'}`, serial, 'Scrcpy');
      return false;
    }
  }

  closeLiveView(serial: string): boolean {
    const activeProcess = this.activeProcesses.get(serial);

    if (!activeProcess) {
      logger.warn('No active live view found for this device', serial, 'Scrcpy');
      return false;
    }

    try {
      activeProcess.process.kill();
      this.activeProcesses.delete(serial);
      logger.info('Live view closed', serial, 'Scrcpy');
      return true;
    } catch (error) {
      logger.error(`Failed to close live view: ${error instanceof Error ? error.message : 'Unknown error'}`, serial, 'Scrcpy');
      return false;
    }
  }

  closeAll(): void {
    logger.info(`Closing ${this.activeProcesses.size} active live view(s)`, undefined, 'Scrcpy');

    for (const [serial, activeProcess] of this.activeProcesses.entries()) {
      try {
        activeProcess.process.kill();
        logger.info('Live view closed', serial, 'Scrcpy');
      } catch (error) {
        logger.error(`Failed to close live view: ${error instanceof Error ? error.message : 'Unknown error'}`, serial, 'Scrcpy');
      }
    }

    this.activeProcesses.clear();
  }

  getActiveDevices(): string[] {
    return Array.from(this.activeProcesses.keys());
  }

  getActiveCount(): number {
    return this.activeProcesses.size;
  }
}

export const scrcpyService = new ScrcpyService();
