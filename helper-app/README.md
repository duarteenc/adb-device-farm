# GenFarmer Wallpaper Helper APK

## Quick Start

Since you don't have Android SDK installed locally, here are your options:

### Option 1: Use Online APK Builder
1. Go to https://www.apk-builder.app/ or https://appsgeyser.com/
2. Upload the Java source files from `src/main/java/`
3. Upload the AndroidManifest.xml
4. Build and download the APK

### Option 2: Use Android Studio (Recommended)
1. Install Android Studio from https://developer.android.com/studio
2. Open this helper-app folder as a project
3. Build > Build Bundle(s) / APK(s) > Build APK(s)
4. The APK will be in `app/build/outputs/apk/debug/`

### Option 3: Pre-built APK
I'll create a minimal working APK for you using a different approach...

## How to Use Once Built

1. Install on all devices:
```bash
adb install GenFarmerWallpaperHelper.apk
```

2. Set wallpaper via ADB:
```bash
adb shell am start -n com.genfarmer.wallpaperhelper/.SetWallpaperActivity -e file "/sdcard/wallpaper.png"
```

## Current Approach
Since building APK requires Android SDK, I'm updating the main app to use the helper APK approach.
The app will check if the helper is installed, and if not, will fall back to the standard method.
