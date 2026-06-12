# Upstream provenance

This project (v2.0) is a **fork of [QtScrcpy](https://github.com/barry-ran/QtScrcpy)**
by barry-ran, which is itself based on [Genymobile/scrcpy](https://github.com/Genymobile/scrcpy).
Both are licensed under the **Apache License 2.0** (see `LICENSE`). We retain that
license and attribution.

## Vendored commit (base of this fork)

The full QtScrcpy source tree was vendored into this repository (flattened — the
`QtScrcpyCore` submodule was materialized in place) at:

| Component         | Repository                                          | Commit                                     |
| ----------------- | --------------------------------------------------- | ------------------------------------------ |
| QtScrcpy (app)    | `github.com/barry-ran/QtScrcpy`                     | `3e8892649d1a36982197f4fd3664bc1321bb13b2` |
| QtScrcpyCore (lib)| `github.com/barry-ran/QtScrcpyCore`                 | `cef8558255b2f90461dad21c062e240aeba18f23` |

Vendored on 2026-06-12.

## Pulling upstream changes later

Because the tree was flattened (no submodule), to diff against or pull a newer
upstream you can add it as a remote and cherry-pick / merge selectively:

```bash
git remote add upstream https://github.com/barry-ran/QtScrcpy.git
git fetch upstream
# Inspect what changed since our base commit:
git diff 3e8892649d1a36982197f4fd3664bc1321bb13b2 upstream/master -- QtScrcpy/
```

The original upstream READMEs are kept as `README.upstream.md` and
`docs/README_zh.upstream.md` for reference.
