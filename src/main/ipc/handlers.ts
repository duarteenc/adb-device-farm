import { ipcMain } from 'electron';
import { deviceService } from '@main/device/device.service';
import { adbService } from '@main/adb/adb.service';
import { logger } from '@main/logger/logger.service';
import type { Device, ADBResult } from '@shared/types';

export function registerIPCHandlers(): void {
  // Device Management
  ipcMain.handle('devices:discover', async (): Promise<Device[]> => {
    logger.info('Discovering devices', undefined, 'IPC');
    return await deviceService.discoverDevices();
  });

  ipcMain.handle('devices:getAll', (): Device[] => {
    return deviceService.getAllDevices();
  });

  ipcMain.handle('devices:connect', async (_, ip: string, port?: number): Promise<boolean> => {
    logger.info(`Connecting to device at ${ip}:${port || 5555}`, undefined, 'IPC');
    return await deviceService.connectDevice(ip, port);
  });

  ipcMain.handle('devices:disconnect', async (_, ip: string, port?: number): Promise<boolean> => {
    logger.info(`Disconnecting from device at ${ip}:${port || 5555}`, undefined, 'IPC');
    return await deviceService.disconnectDevice(ip, port);
  });

  // ADB Commands
  ipcMain.handle('adb:tap', async (_, serial: string, x: number, y: number): Promise<ADBResult> => {
    logger.info(`Tap at (${x}, ${y})`, serial, 'ADB');
    return await adbService.tapScreen(serial, x, y);
  });

  ipcMain.handle('adb:swipe', async (_, serial: string, x1: number, y1: number, x2: number, y2: number, duration?: number): Promise<ADBResult> => {
    logger.info(`Swipe from (${x1}, ${y1}) to (${x2}, ${y2})`, serial, 'ADB');
    return await adbService.swipe(serial, x1, y1, x2, y2, duration);
  });

  ipcMain.handle('adb:inputText', async (_, serial: string, text: string): Promise<ADBResult> => {
    logger.info(`Input text: ${text.substring(0, 20)}...`, serial, 'ADB');
    return await adbService.inputText(serial, text);
  });

  ipcMain.handle('adb:pressKey', async (_, serial: string, keycode: string | number): Promise<ADBResult> => {
    logger.info(`Press key: ${keycode}`, serial, 'ADB');
    return await adbService.pressKey(serial, keycode);
  });

  ipcMain.handle('adb:screenshot', async (_, serial: string, outputPath: string): Promise<ADBResult> => {
    logger.info(`Taking screenshot`, serial, 'ADB');
    return await adbService.takeScreenshot(serial, outputPath);
  });

  ipcMain.handle('adb:install', async (_, serial: string, apkPath: string): Promise<ADBResult> => {
    logger.info(`Installing APK: ${apkPath}`, serial, 'ADB');
    return await adbService.installApp(serial, apkPath);
  });

  ipcMain.handle('adb:uninstall', async (_, serial: string, packageName: string): Promise<ADBResult> => {
    logger.info(`Uninstalling app: ${packageName}`, serial, 'ADB');
    return await adbService.uninstallApp(serial, packageName);
  });

  ipcMain.handle('adb:startApp', async (_, serial: string, packageName: string): Promise<ADBResult> => {
    logger.info(`Starting app: ${packageName}`, serial, 'ADB');
    return await adbService.startApp(serial, packageName);
  });

  ipcMain.handle('adb:stopApp', async (_, serial: string, packageName: string): Promise<ADBResult> => {
    logger.info(`Stopping app: ${packageName}`, serial, 'ADB');
    return await adbService.stopApp(serial, packageName);
  });

  ipcMain.handle('adb:clearAppData', async (_, serial: string, packageName: string): Promise<ADBResult> => {
    logger.info(`Clearing app data: ${packageName}`, serial, 'ADB');
    return await adbService.clearAppData(serial, packageName);
  });

  ipcMain.handle('adb:reboot', async (_, serial: string): Promise<ADBResult> => {
    logger.warn(`Rebooting device`, serial, 'ADB');
    return await adbService.rebootDevice(serial);
  });

  // Logger
  ipcMain.handle('logs:getAll', (_, limit?: number) => {
    return logger.getLogs(limit);
  });

  ipcMain.handle('logs:clear', () => {
    logger.clearLogs();
  });

  ipcMain.handle('logs:getByDevice', (_, serial: string) => {
    return logger.getLogsByDevice(serial);
  });

  logger.info('IPC handlers registered', undefined, 'IPC');
}
