#!/usr/bin/env python3
"""Install the Qt 6 MSVC 2022 x64 kit from the OFFICIAL Qt download repository.

This is what `aqtinstall` does under the hood, reduced to what this project needs
(qtbase, qtsvg, qttools, qtdeclarative, qtmultimedia, qtshadertools, qtimageformats
and the d3dcompiler/opengl32sw runtime helpers). Archives are fetched from
https://download.qt.io (the same repository the Qt Online Installer uses), verified
against the published SHA-256 sums, and extracted to <out>/<version>/msvc2022_64.

Usage:  python scripts/install-qt.py [--version 6.11.1] [--out C:/Qt]
Requires: pip install py7zr
"""
import argparse
import hashlib
import os
import shutil
import sys
import urllib.request
import xml.etree.ElementTree as ET

try:
    import py7zr
except ImportError:  # pragma: no cover
    sys.exit("py7zr is required: python -m pip install py7zr")

BASE_ARCHIVES = ["qtbase", "qtsvg", "qttools", "qtdeclarative", "d3dcompiler", "opengl32sw"]
ADDONS = ["qtmultimedia", "qtshadertools", "qtimageformats"]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="6.11.1")
    ap.add_argument("--out", default=r"C:\Qt")
    ap.add_argument("--cache", default=os.path.join(os.environ.get("TEMP", "."), "qt-archives"))
    args = ap.parse_args()

    ver = args.version
    vtag = ver.replace(".", "")
    base = f"https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_{vtag}/qt6_{vtag}_msvc2022_64/"
    dest = os.path.join(args.out, ver, "msvc2022_64")
    if os.path.exists(os.path.join(dest, "lib", "cmake", "Qt6", "Qt6Config.cmake")):
        print(f"Qt {ver} already installed at {dest}")
        return 0

    print("Reading package index", base + "Updates.xml")
    root = ET.fromstring(urllib.request.urlopen(base + "Updates.xml", timeout=60).read())
    jobs = []
    for p in root.findall("PackageUpdate"):
        name = p.findtext("Name") or ""
        pver = p.findtext("Version") or ""
        archives = [a.strip() for a in (p.findtext("DownloadableArchives") or "").split(",") if a.strip()]
        if name == f"qt.qt6.{vtag}.win64_msvc2022_64":
            jobs += [(name, pver, a) for a in archives if any(a.startswith(w) for w in BASE_ARCHIVES)]
        for ad in ADDONS:
            if name == f"qt.qt6.{vtag}.addons.{ad}.win64_msvc2022_64":
                jobs += [(name, pver, a) for a in archives]
    if not jobs:
        sys.exit(f"No archives found for Qt {ver} — check https://download.qt.io for available versions")

    os.makedirs(args.cache, exist_ok=True)
    stage = dest + ".partial"
    os.makedirs(stage, exist_ok=True)
    for name, pver, a in jobs:
        url = f"{base}{name}/{pver}{a}"
        local = os.path.join(args.cache, a)
        if not os.path.exists(local):
            print("Downloading", a)
            urllib.request.urlretrieve(url, local + ".part")
            try:
                sha = urllib.request.urlopen(url + ".sha256", timeout=60).read().decode().split()[0]
                h = hashlib.sha256()
                with open(local + ".part", "rb") as f:
                    for chunk in iter(lambda: f.read(1 << 20), b""):
                        h.update(chunk)
                if h.hexdigest() != sha:
                    sys.exit(f"SHA-256 mismatch for {a}")
            except urllib.error.URLError as e:
                print("  (no .sha256 published, skipping verification:", e, ")")
            os.replace(local + ".part", local)
        print("Extracting", a)
        with py7zr.SevenZipFile(local, "r") as z:
            z.extractall(stage)
    # The 6.8+ archives extract flat (bin/, lib/, ...) — move into the versioned kit dir.
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    os.replace(stage, dest)
    with open(os.path.join(dest, "bin", "qt.conf"), "w") as f:
        f.write("[Paths]\nPrefix=..\n")
    print("Installed Qt", ver, "to", dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
