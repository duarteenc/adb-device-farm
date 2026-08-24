# Keep awake

Farm phones must not sleep while they are managed. `KeepAwakeManager` applies a
policy per device (device setting > group setting > global Settings › Keep
Awake, default **ON**) and verifies that it stuck.

## Commands

Apply (one `adb shell`, values read back afterwards):

```sh
svc power stayon true
settings put global stay_on_while_plugged_in 7
settings put system screen_off_timeout 2147483647
echo SOWP=$(settings get global stay_on_while_plugged_in)
echo SOT=$(settings get system screen_off_timeout)
```

Status shown in the device inspector / list view / dashboard:

| Status | Meaning |
| --- | --- |
| `Active` | both values verified (`stay_on_while_plugged_in & 7 == 7`, timeout ≥ 2147483647) |
| `Failed: …` | the shell failed or a vendor ROM silently rejected a value (diagnostic text includes what came back) |
| `Awake but locked` | display on, keyguard showing — the lock is **never** bypassed |
| `Display off` | display off and *Wake sleeping devices* is disabled |
| `Off` | policy OFF for this device; defaults restored (`stayon false`, timeout 30 s) |

Restore (Devices › context menu › *Restore default timeout*):

```sh
svc power stayon false
settings put global stay_on_while_plugged_in 0
settings put system screen_off_timeout 30000
```

## Health check

Every Settings › *check interval* (default 60 s) the manager probes a quarter of
the online fleet per tick (round-robin, so 100 phones never all get polled at
once):

```sh
dumpsys power | grep -E 'mWakefulness=|Display Power'
dumpsys window policy | grep -E 'mShowingLockscreen|isKeyguardShowing|showing='
```

If the display is off while the policy is ON and *Wake sleeping devices* is ON,
the manager sends `input keyevent KEYCODE_WAKEUP` and re-applies the policy
(a display that keeps turning off means the settings did not stick).

## When it is re-applied

- device enters an online state (first discovery, reconnect, after reboot, USB → WiFi transition)
- global or group policy changes
- workflow node *Apply keep-awake policy*
- manually from the Devices page (*Keep awake › Apply*)

The scrcpy session itself is started with `stayAwake=true` as well, so a
mirrored phone additionally holds the server-side wake lock.
