# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned for v0.2.0 (Phase 2)
- File transfer (push/pull)
- Screen recording
- Custom shell commands
- Gesture macros
- Enhanced device monitoring

## [0.1.0] - 2026-06-12

### Added
- Initial release
- Electron + React + TypeScript architecture
- Modern dark UI with neon red theme and animated starry background
- ADB service layer for all device operations
- Device management service with auto-discovery
- Multi-device selection and batch operations
- Quick actions panel:
  - Screen tap (coordinates)
  - Text input
  - Key events (Home, Back, Recents)
  - App control (start, stop, clear data)
  - System control (reboot)
- Real-time logging system with filtering
- Device information display:
  - Model, manufacturer, Android version
  - Battery level, screen resolution
  - Connection type (USB/WiFi)
  - Online/offline/unauthorized status
- Network device connection via IP
- Professional project structure
- Comprehensive README with setup instructions
- Testing documentation
- Complete roadmap (TODO.md)

### Technical
- TypeScript with strict mode
- ESLint with zero warnings/errors policy
- Tailwind CSS for styling
- Zustand for state management
- IPC-based architecture for main/renderer communication
- Modular service architecture
- Type-safe codebase

### Security
- LAN-only operation
- No internet exposure
- Confirmation for destructive operations
- Input sanitization for text commands

### Developer Experience
- Fast development with Vite + Hot Module Replacement
- Type checking with `npm run type-check`
- Linting with `npm run lint`
- Separate build scripts for Vite and Electron Builder
- Clear git workflow with main and develop branches

## [0.0.0] - 2026-06-12

### Repository Setup
- Created GitHub repository
- Added collaborator GabrielGarciaRivas with admin permissions
- Initial project structure

---

[Unreleased]: https://github.com/duarteenc/adb-device-farm/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/duarteenc/adb-device-farm/releases/tag/v0.1.0
