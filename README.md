# ADB Device Farm 🚀

Professional Android Device Farm Management Tool - Control multiple Android devices via ADB with a modern, beautiful interface.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Version](https://img.shields.io/badge/version-0.1.0-red.svg)

## 🎯 Overview

ADB Device Farm is a powerful desktop application built to manage and control multiple Android devices simultaneously through ADB (Android Debug Bridge). Whether you're managing a device farm, testing applications across multiple devices, or automating Android workflows, this tool provides an intuitive and efficient interface.

## ✨ Features

### Device Management
- ✅ **Automatic Detection**: Discovers USB and network-connected devices
- ✅ **WiFi Connection**: Connect devices over local network via IP
- ✅ **Real-time Status**: Live monitoring of device status (online, offline, unauthorized)
- ✅ **Device Information**: Display model, manufacturer, Android version, battery, resolution
- ✅ **Multi-selection**: Select and control multiple devices simultaneously

### Remote Control
- 📱 **Screen Input**: Send taps, swipes, and text input
- ⌨️ **Key Events**: Home, Back, Recent Apps, and custom key events
- 📸 **Screenshots**: Capture device screens
- 🎮 **Batch Operations**: Execute actions on multiple devices at once

### App Management
- 📦 **Install/Uninstall**: Manage APK installations across devices
- 🚀 **Start/Stop Apps**: Launch and force-stop applications
- 🗑️ **Clear Data**: Clear app data and cache
- 📋 **Package Management**: Control apps via package name

### System Control
- 🔄 **Reboot Devices**: Safely restart devices
- 🔌 **Connection Management**: Connect/disconnect network devices
- 📊 **Battery Monitoring**: Track battery levels
- 🔍 **Device Discovery**: Auto-refresh device list

### Advanced Features
- 📝 **Real-time Logging**: Comprehensive log system with filtering
- 🎨 **Modern UI**: Beautiful dark theme with neon red accents and starry background
- ⚡ **High Performance**: Built with Electron, React, and TypeScript
- 🔒 **Local Network Only**: Secure, LAN-only operation

## 🛠️ Tech Stack

- **Electron**: Desktop application framework
- **React**: UI library
- **TypeScript**: Type-safe development
- **Vite**: Lightning-fast build tool
- **Tailwind CSS**: Utility-first styling
- **Zustand**: State management
- **ADB**: Android Debug Bridge

## 📋 Requirements

### System Requirements
- **OS**: Windows 10/11, macOS 10.13+, or Linux
- **Node.js**: v18 or higher
- **npm**: v8 or higher

### Android Requirements
- **ADB**: Android Platform Tools must be installed and available in PATH
- **USB Debugging**: Enabled on Android devices
- **Network ADB** (optional): Enabled for wireless connections

## 🚀 Installation

### 1. Install Android Platform Tools

**Windows:**
```bash
# Download from: https://developer.android.com/studio/releases/platform-tools
# Add to PATH environment variable
```

**macOS:**
```bash
brew install android-platform-tools
```

**Linux:**
```bash
sudo apt-get install android-tools-adb
```

### 2. Verify ADB Installation
```bash
adb --version
```

### 3. Clone and Install

```bash
git clone https://github.com/duarteenc/adb-device-farm.git
cd adb-device-farm
npm install
```

## 🎮 Usage

### Development Mode
```bash
npm run electron:dev
```

This will:
1. Start Vite dev server on port 5173
2. Launch Electron with hot reload enabled
3. Open DevTools automatically

### Production Build
```bash
npm run build
```

Built applications will be available in the `release/` directory.

## 📱 Connecting Devices

### USB Connection
1. Enable USB Debugging on your Android device
2. Connect device via USB cable
3. Accept USB debugging authorization on device
4. Device will appear automatically in the app

### WiFi/Network Connection
1. Enable USB Debugging on your Android device
2. Connect device via USB first
3. Run: `adb tcpip 5555`
4. Disconnect USB and note device IP address
5. In the app, click "Connect Device"
6. Enter device IP and port (default: 5555)
7. Click "Connect"

**Alternative Method:**
```bash
# Connect via command line
adb connect 192.168.1.100:5555

# Verify connection
adb devices
```

## ⚠️ Security Warnings

### ADB Security Best Practices

- ⚠️ **LAN Only**: Only use on trusted local networks
- ⚠️ **No Internet Exposure**: Never expose ADB ports to the internet
- ⚠️ **Firewall**: Configure firewall to block external ADB access
- ⚠️ **USB Debugging**: Disable when not needed
- ⚠️ **Network ADB**: Disable when not actively using wireless connections
- ⚠️ **Authorization**: Only authorize trusted computers on your devices
- ⚠️ **Destructive Commands**: Always confirm before rebooting or clearing data

### Dangerous Operations

The following operations require user confirmation:
- ❌ Reboot devices
- ❌ Clear app data
- ❌ Uninstall applications
- ❌ Execute custom shell commands

## 🗂️ Project Structure

```
adb-device-farm/
├── src/
│   ├── main/              # Electron Main Process
│   │   ├── adb/           # ADB service layer
│   │   ├── device/        # Device management
│   │   ├── logger/        # Logging system
│   │   └── ipc/           # IPC handlers
│   ├── renderer/          # React Frontend
│   │   ├── components/    # UI components
│   │   ├── stores/        # Zustand stores
│   │   ├── utils/         # Utilities
│   │   └── types/         # TypeScript types
│   └── shared/            # Shared code
├── tests/                 # Test files
├── resources/             # App resources
└── docs/                  # Documentation
```

## 🧪 Testing

```bash
# Type checking
npm run type-check

# Linting
npm run lint

# Full build test
npm run build
```

## 📝 Available Commands

```bash
npm run dev              # Start Vite dev server
npm run electron:dev     # Run in development mode
npm run build           # Build for production
npm run lint            # Run ESLint
npm run type-check      # Run TypeScript compiler check
npm run preview         # Preview production build
```

## 🐛 Troubleshooting

### ADB Not Found
```bash
# Verify ADB installation
adb --version

# Add ADB to PATH (Windows)
setx PATH "%PATH%;C:\path\to\platform-tools"

# Add ADB to PATH (macOS/Linux)
export PATH=$PATH:/path/to/platform-tools
```

### Device Unauthorized
1. Disconnect and reconnect USB
2. Check device for authorization popup
3. Accept "Allow USB debugging"
4. Check "Always allow from this computer"

### Device Offline
1. Restart ADB server: `adb kill-server && adb start-server`
2. Reconnect device
3. Try different USB cable/port
4. Restart device

### Network Connection Failed
1. Verify device is on same network
2. Check firewall settings
3. Ensure port 5555 is not blocked
4. Restart ADB server

## 🛣️ Roadmap

### Phase 2 - Advanced Control (Coming Soon)
- [ ] File transfer (push/pull)
- [ ] Screen recording
- [ ] Custom command execution
- [ ] Gesture macros (swipe patterns)

### Phase 3 - Automation (Planned)
- [ ] Macro recording and playback
- [ ] Scheduled task execution
- [ ] Parallel vs sequential execution modes
- [ ] Macro library and sharing

### Phase 4 - Enterprise Features (Future)
- [ ] Device grouping and tagging
- [ ] Export/import device configurations
- [ ] Advanced logging with export
- [ ] Performance monitoring

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## 📄 License

This project is licensed under the MIT License.

## 👨‍💻 Authors

- **duarteenc** - Initial work

## 🙏 Acknowledgments

- Android Platform Tools team
- Electron community
- React and TypeScript communities

## 📞 Support

For issues, questions, or suggestions, please open an issue on GitHub.

---

**⚠️ Disclaimer**: This tool is for legitimate device management purposes only. Always ensure you have proper authorization to control devices. The authors are not responsible for misuse of this software.
