# Build Instructions for GenFarmer Wallpaper Helper

## Method 1: Android Studio (Recommended)

1. **Open Android Studio**

2. **Open Project**
   - File → Open
   - Navigate to `C:\adb-device-farm\WallpaperHelper`
   - Click "OK"

3. **Wait for Gradle Sync**
   - Android Studio will automatically sync Gradle
   - This may take a few minutes the first time

4. **Build APK**
   - Build → Build Bundle(s) / APK(s) → Build APK(s)
   - Or click the hammer icon in the toolbar

5. **Find the APK**
   - After build completes, click "locate" in the notification
   - Or manually: `WallpaperHelper\app\build\outputs\apk\debug\app-debug.apk`

6. **Install on all devices**
   ```bash
   adb install app-debug.apk
   ```

## Method 2: Command Line (Gradle)

```bash
cd C:\adb-device-farm\WallpaperHelper
gradlew assembleDebug
```

APK will be at: `app\build\outputs\apk\debug\app-debug.apk`

## Method 3: Direct Install to Connected Device

In Android Studio:
- Run → Run 'app'
- Select target device
- The app will be installed automatically

## Testing the Helper

After installing on a device, test with:

```bash
# Push a test image
adb push test.png /sdcard/test_wp.png

# Trigger the helper
adb shell am start -n com.genfarmer.wallpaperhelper/.SetWallpaperActivity -e file "/sdcard/test_wp.png"
```

The wallpaper should change immediately without any dialog!

## Troubleshooting

- **Gradle sync failed**: Check internet connection, Android Studio needs to download dependencies
- **SDK not found**: Tools → SDK Manager → Install Android SDK Platform 34
- **Build failed**: Check the "Build" tab at the bottom for error details
