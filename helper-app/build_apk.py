#!/usr/bin/env python3
"""
Simple APK builder for GenFarmer Wallpaper Helper
Requires: Android SDK, Java JDK
"""

import os
import subprocess
import shutil
from pathlib import Path

# Configuration
PACKAGE = "com.genfarmer.wallpaperhelper"
APP_NAME = "GenFarmerWallpaperHelper"
BUILD_DIR = Path("build")
SRC_DIR = Path("src/main")
JAVA_SRC = SRC_DIR / "java"
RES_DIR = SRC_DIR / "res"
MANIFEST = SRC_DIR / "AndroidManifest.xml"

# Android SDK paths (adjust if needed)
ANDROID_HOME = os.environ.get("ANDROID_HOME", "C:/Android/sdk")
BUILD_TOOLS = Path(ANDROID_HOME) / "build-tools" / "33.0.0"
PLATFORM = Path(ANDROID_HOME) / "platforms" / "android-33"
ANDROID_JAR = PLATFORM / "android.jar"

# Tools
AAPT = BUILD_TOOLS / "aapt.exe"
D8 = BUILD_TOOLS / "d8.bat"
ZIPALIGN = BUILD_TOOLS / "zipalign.exe"

def run(cmd, **kwargs):
    """Run command and check result"""
    print(f"Running: {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed with code {result.returncode}")
    return result

def main():
    # Create build directories
    BUILD_DIR.mkdir(exist_ok=True)
    (BUILD_DIR / "gen").mkdir(exist_ok=True)
    (BUILD_DIR / "obj").mkdir(exist_ok=True)
    (BUILD_DIR / "apk").mkdir(exist_ok=True)

    # Create minimal res structure
    RES_DIR.mkdir(parents=True, exist_ok=True)
    (RES_DIR / "values").mkdir(exist_ok=True)

    # Create strings.xml
    strings_xml = RES_DIR / "values" / "strings.xml"
    strings_xml.write_text('''<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="app_name">GenFarmer Helper</string>
</resources>
''')

    # Create drawable directory for icon
    (RES_DIR / "mipmap").mkdir(exist_ok=True)

    print("Step 1: Generate R.java")
    run([
        AAPT, "package", "-f", "-m",
        "-J", BUILD_DIR / "gen",
        "-S", RES_DIR,
        "-M", MANIFEST,
        "-I", ANDROID_JAR
    ])

    print("\nStep 2: Compile Java sources")
    java_files = list((JAVA_SRC / "com" / "genfarmer" / "wallpaperhelper").glob("*.java"))
    r_java = BUILD_DIR / "gen" / "com" / "genfarmer" / "wallpaperhelper" / "R.java"

    run([
        "javac",
        "-d", BUILD_DIR / "obj",
        "-classpath", ANDROID_JAR,
        "-sourcepath", JAVA_SRC,
        *java_files,
        r_java
    ])

    print("\nStep 3: Convert to DEX")
    class_files = list((BUILD_DIR / "obj").rglob("*.class"))
    run([
        D8,
        "--lib", ANDROID_JAR,
        "--output", BUILD_DIR / "apk",
        *class_files
    ])

    print("\nStep 4: Package APK")
    unsigned_apk = BUILD_DIR / "app-unsigned.apk"
    run([
        AAPT, "package", "-f",
        "-M", MANIFEST,
        "-S", RES_DIR,
        "-I", ANDROID_JAR,
        "-F", unsigned_apk,
        BUILD_DIR / "apk"
    ])

    print("\nStep 5: Sign APK (debug)")
    # Create debug keystore if it doesn't exist
    keystore = Path("debug.keystore")
    if not keystore.exists():
        run([
            "keytool", "-genkeypair",
            "-keystore", keystore,
            "-alias", "androiddebugkey",
            "-keypass", "android",
            "-storepass", "android",
            "-keyalg", "RSA",
            "-keysize", "2048",
            "-validity", "10000",
            "-dname", "CN=Android Debug,O=Android,C=US"
        ])

    run([
        "jarsigner",
        "-keystore", keystore,
        "-storepass", "android",
        "-keypass", "android",
        unsigned_apk,
        "androiddebugkey"
    ])

    print("\nStep 6: Zipalign")
    final_apk = BUILD_DIR / f"{APP_NAME}.apk"
    if final_apk.exists():
        final_apk.unlink()

    run([
        ZIPALIGN, "-f", "4",
        unsigned_apk,
        final_apk
    ])

    print(f"\n✓ APK built successfully: {final_apk.absolute()}")
    print(f"\nTo install: adb install {final_apk}")

if __name__ == "__main__":
    main()
