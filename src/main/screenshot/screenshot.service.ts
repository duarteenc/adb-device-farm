import { adbService } from '@main/adb/adb.service';
import { logger } from '@main/logger/logger.service';
import path from 'path';
import fs from 'fs';
import { app } from 'electron';

interface ScreenshotCache {
  serial: string;
  imagePath: string;
  timestamp: number;
  base64?: string;
}

export class ScreenshotService {
  private cache: Map<string, ScreenshotCache> = new Map();
  private captureIntervals: Map<string, NodeJS.Timeout> = new Map();
  private screenshotDir: string;

  constructor() {
    // Create screenshots directory
    this.screenshotDir = path.join(app.getPath('temp'), 'adb-device-farm-screenshots');
    if (!fs.existsSync(this.screenshotDir)) {
      fs.mkdirSync(this.screenshotDir, { recursive: true });
    }
  }

  private getScreenshotPath(serial: string): string {
    // Clean serial for filename
    const cleanSerial = serial.replace(/[^a-zA-Z0-9]/g, '_');
    return path.join(this.screenshotDir, `${cleanSerial}.png`);
  }

  async captureScreenshot(serial: string): Promise<string | null> {
    try {
      const outputPath = this.getScreenshotPath(serial);
      const result = await adbService.takeScreenshot(serial, outputPath);

      if (result.success && fs.existsSync(outputPath)) {
        // Read as base64
        const imageBuffer = fs.readFileSync(outputPath);
        const base64Image = imageBuffer.toString('base64');

        // Update cache
        this.cache.set(serial, {
          serial,
          imagePath: outputPath,
          timestamp: Date.now(),
          base64: base64Image,
        });

        return base64Image;
      }

      return null;
    } catch (error) {
      logger.error(`Failed to capture screenshot: ${error instanceof Error ? error.message : 'Unknown error'}`, serial, 'Screenshot');
      return null;
    }
  }

  async getCachedScreenshot(serial: string): Promise<string | null> {
    const cached = this.cache.get(serial);
    if (cached && cached.base64) {
      // Check if cache is fresh (less than 5 seconds old)
      const age = Date.now() - cached.timestamp;
      if (age < 5000) {
        return cached.base64;
      }
    }

    // Capture new screenshot
    return await this.captureScreenshot(serial);
  }

  startAutoCapture(serial: string, intervalMs: number = 2000): void {
    // Stop existing interval if any
    this.stopAutoCapture(serial);

    // Capture immediately
    this.captureScreenshot(serial);

    // Start interval
    const interval = setInterval(() => {
      this.captureScreenshot(serial);
    }, intervalMs);

    this.captureIntervals.set(serial, interval);
    logger.info(`Auto-capture started (${intervalMs}ms interval)`, serial, 'Screenshot');
  }

  stopAutoCapture(serial: string): void {
    const interval = this.captureIntervals.get(serial);
    if (interval) {
      clearInterval(interval);
      this.captureIntervals.delete(serial);
      logger.info('Auto-capture stopped', serial, 'Screenshot');
    }
  }

  stopAllAutoCapture(): void {
    for (const [serial, interval] of this.captureIntervals.entries()) {
      clearInterval(interval);
      logger.info('Auto-capture stopped', serial, 'Screenshot');
    }
    this.captureIntervals.clear();
  }

  getActiveAutoCaptures(): string[] {
    return Array.from(this.captureIntervals.keys());
  }

  clearCache(): void {
    this.cache.clear();
  }

  cleanup(): void {
    this.stopAllAutoCapture();
    this.clearCache();

    // Delete screenshot files
    try {
      if (fs.existsSync(this.screenshotDir)) {
        const files = fs.readdirSync(this.screenshotDir);
        for (const file of files) {
          fs.unlinkSync(path.join(this.screenshotDir, file));
        }
      }
    } catch (error) {
      logger.error(`Failed to cleanup screenshots: ${error instanceof Error ? error.message : 'Unknown error'}`, undefined, 'Screenshot');
    }
  }
}

export const screenshotService = new ScreenshotService();
