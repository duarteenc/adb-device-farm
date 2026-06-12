import { app, BrowserWindow } from 'electron';
import path from 'path';
import { fileURLToPath } from 'url';
import { registerIPCHandlers } from './ipc/handlers';
import { deviceService } from './device/device.service';
import { logger } from './logger/logger.service';
import { scrcpyService } from './scrcpy/scrcpy.service';
import { screenshotService } from './screenshot/screenshot.service';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

let mainWindow: BrowserWindow | null = null;

function createWindow(): void {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    minWidth: 1200,
    minHeight: 700,
    backgroundColor: '#0A0A0F',
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
      preload: path.join(__dirname, 'preload.js'),
    },
    titleBarStyle: 'default',
    frame: true,
    show: false,
  });

  // Set logger main window
  logger.setMainWindow(mainWindow);

  // Load the app
  if (process.env.NODE_ENV === 'development') {
    mainWindow.loadURL('http://localhost:5173');
    mainWindow.webContents.openDevTools();
  } else {
    mainWindow.loadFile(path.join(__dirname, '../dist/index.html'));
  }

  mainWindow.once('ready-to-show', () => {
    mainWindow?.show();
    logger.info('Application started', undefined, 'Main');
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

app.whenReady().then(() => {
  registerIPCHandlers();
  createWindow();

  // Start auto-refresh of devices
  deviceService.startAutoRefresh(5000);

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  deviceService.stopAutoRefresh();
  scrcpyService.closeAll();
  screenshotService.cleanup();
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('before-quit', () => {
  logger.info('Application shutting down', undefined, 'Main');
  deviceService.stopAutoRefresh();
  scrcpyService.closeAll();
  screenshotService.cleanup();
});
