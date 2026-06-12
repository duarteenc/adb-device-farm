import { adbService } from '@main/adb/adb.service';
import type { Device, DeviceStatus } from '@shared/types';

export class DeviceService {
  private devices: Map<string, Device> = new Map();
  private refreshInterval: NodeJS.Timeout | null = null;

  async discoverDevices(): Promise<Device[]> {
    try {
      const serials = await adbService.getDevices();
      const discoveredDevices: Device[] = [];

      for (const serial of serials) {
        const status = await adbService.getDeviceStatus(serial);
        const connectionType = serial.includes(':') ? 'wifi' : 'usb';

        let device: Device = {
          serial,
          status: this.normalizeStatus(status),
          connectionType,
          lastSeen: Date.now(),
        };

        if (status === 'device') {
          const info = await this.getDeviceInfo(serial);
          device = { ...device, ...info };
        }

        this.devices.set(serial, device);
        discoveredDevices.push(device);
      }

      // Mark devices that are no longer visible as disconnected
      for (const [serial, device] of this.devices.entries()) {
        if (!serials.includes(serial)) {
          device.status = 'disconnected';
          discoveredDevices.push(device);
        }
      }

      return discoveredDevices;
    } catch (error) {
      console.error('[DeviceService] Failed to discover devices:', error);
      return [];
    }
  }

  private normalizeStatus(status: string): DeviceStatus {
    switch (status) {
      case 'device':
        return 'online';
      case 'offline':
        return 'offline';
      case 'unauthorized':
        return 'unauthorized';
      default:
        return 'disconnected';
    }
  }

  private async getDeviceInfo(serial: string): Promise<Partial<Device>> {
    try {
      const [model, androidVersion, manufacturer, deviceName, resolution, battery] = await Promise.all([
        adbService.getDeviceProperty(serial, 'ro.product.model'),
        adbService.getDeviceProperty(serial, 'ro.build.version.release'),
        adbService.getDeviceProperty(serial, 'ro.product.manufacturer'),
        adbService.getDeviceProperty(serial, 'ro.product.device'),
        this.getResolution(serial),
        adbService.getBatteryLevel(serial),
      ]);

      return {
        model: model || 'Unknown',
        androidVersion: androidVersion || 'Unknown',
        manufacturer: manufacturer || 'Unknown',
        deviceName: deviceName || 'Unknown',
        resolution: resolution || 'Unknown',
        battery: battery >= 0 ? battery : undefined,
        ip: serial.includes(':') ? serial.split(':')[0] : undefined,
      };
    } catch (error) {
      console.error(`[DeviceService] Failed to get device info for ${serial}:`, error);
      return {};
    }
  }

  private async getResolution(serial: string): Promise<string> {
    try {
      const result = await adbService.executeDeviceCommand(serial, 'shell', ['wm size']);
      if (result.success) {
        const match = result.output.match(/Physical size: (\d+x\d+)/);
        return match ? match[1] : 'Unknown';
      }
    } catch (error) {
      console.error(`[DeviceService] Failed to get resolution for ${serial}:`, error);
    }
    return 'Unknown';
  }

  async connectDevice(ip: string, port: number = 5555): Promise<boolean> {
    try {
      const result = await adbService.connectDevice(ip, port);
      if (result.success) {
        await this.discoverDevices();
        return true;
      }
      return false;
    } catch (error) {
      console.error('[DeviceService] Failed to connect device:', error);
      return false;
    }
  }

  async disconnectDevice(ip: string, port: number = 5555): Promise<boolean> {
    try {
      const result = await adbService.disconnectDevice(ip, port);
      if (result.success) {
        const serial = `${ip}:${port}`;
        this.devices.delete(serial);
        return true;
      }
      return false;
    } catch (error) {
      console.error('[DeviceService] Failed to disconnect device:', error);
      return false;
    }
  }

  getDevice(serial: string): Device | undefined {
    return this.devices.get(serial);
  }

  getAllDevices(): Device[] {
    return Array.from(this.devices.values());
  }

  startAutoRefresh(intervalMs: number = 5000): void {
    if (this.refreshInterval) {
      clearInterval(this.refreshInterval);
    }

    this.refreshInterval = setInterval(() => {
      this.discoverDevices();
    }, intervalMs);

    // Initial discovery
    this.discoverDevices();
  }

  stopAutoRefresh(): void {
    if (this.refreshInterval) {
      clearInterval(this.refreshInterval);
      this.refreshInterval = null;
    }
  }
}

export const deviceService = new DeviceService();
