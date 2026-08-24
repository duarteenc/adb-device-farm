# Automation Studio

Workflows are JSON documents executed per device by `WorkflowEngine`, edited
visually in *Automations* (node editor), recorded from the master device, or
generated (optionally) from natural language by a **local** model.

## Model

```json
{
  "id": "…", "name": "Example", "version": 1,
  "variables": { "count": 3 },
  "runDefaults": { "targetsMode": "selection", "concurrency": 5 },
  "nodes": [
    { "id": "n1", "type": "flow.start", "params": {}, "pos": [40, 120] },
    { "id": "n2", "type": "app.launch", "params": { "package": "com.android.settings" },
      "retryCount": 2, "retryDelayMs": 1000, "timeoutMs": 0, "onFailure": "fail", "pos": [230, 120] },
    { "id": "n3", "type": "flow.end", "params": {}, "pos": [420, 120] }
  ],
  "connections": [ { "from": "n1", "port": "out", "to": "n2" }, { "from": "n2", "port": "out", "to": "n3" } ]
}
```

Positions are for the editor only; the engine ignores them. `WorkflowValidator`
checks for a single Start, known node types, required parameters, dangling
connections and unreachable nodes.

## Nodes (`NodeCatalog`)

| Category | Types |
| --- | --- |
| Flow | `flow.start` (default targets, concurrency, sequential), `flow.end`, `flow.fail`, `flow.runWorkflow` (sub-workflow, depth ≤ 5) |
| Device | `device.property` → variable, `device.wake`, `device.keepAwake` |
| Interaction | `input.tap`, `input.doubleTap`, `input.longPress`, `input.swipe`, `input.scroll`, `input.text`, `input.key`, `input.back`, `input.home`, `input.recent` |
| Application | `app.launch`, `app.forceStop`, `app.clearData`*, `app.install`, `app.uninstall`*, `app.waitFor` (found/timeout), `app.isForeground` (true/false) |
| Timing | `time.wait`, `time.waitRandom` (min/max — timing resilience, not evasion) |
| Logic | `logic.if` (true/false), `logic.loop` (body/done, index variable), `logic.while` (condition, max iterations), `logic.break`, `logic.continue`, `logic.switch` (case:X/default) |
| Variables | `var.set`, `var.increment`, `var.listAppend` |
| ADB | `adb.shell` (save output), `adb.exec`*, `adb.getprop` |
| Screen | `screen.screenshot`, `screen.findImage`, `screen.waitForImage` (until visible / until gone), `screen.tapImage`, `screen.ocr`, `screen.waitForText`, `screen.tapText`, `ui.waitForElement`, `ui.tapElement`, `ui.exists`, `ui.dump` |
| Files | `file.push`, `file.pull`, `file.exists`, `file.delete`* |
| Logging | `log.message`, `log.screenshot`, `log.errorReport` (screenshot + UI dump + variables) |

`*` = *risky*: the Run dialog and the AI review list these and ask for
confirmation, in addition to the "this will affect N devices" confirmation for
≥ 10 targets.

Coordinates ≤ 1 are screen fractions (resolution independent); > 1 are pixels.
Every node has **retries, retry delay, timeout and on-failure** (fail the
device with an error screenshot / continue / stop) — Automation Studio ›
property panel › *Failure handling*.

### Expressions

`${var}`, `${match.x}`, `${device.model}`, `${list.length}` substitute anywhere.
Conditions: `${battery} < 20 && ${charging} == false`, `${text} contains "OK"`,
`${text} matches ^Login`, `!${found}`; operators `== != < <= > >= contains
startsWith endsWith matches`, combined with `&&`/`||`.

Variables set by nodes: `screen.findImage/waitForImage` → `{x, y, px, py, width,
height, score}`; `screen.waitForText`/`ui.waitForElement` → `{x, y, px, py,
width, height}`; `adb.shell` → text; `screen.ocr` → text; `device.property` →
value. The `device` map is always available (`id, number, name, model, ip,
group, battery`).

## Execution (`AutomationRun`)

- one worker per device on the `automation` lane, at most *Concurrency* at a time
  (`Sequential` = 1); a failing device never affects the others
- Start · Pause · Resume · Stop · Cancel · **Retry failed** (only the failed
  devices re-run) · Clone (duplicate workflow)
- status per device: queued / running / ok / failed / cancelled, current node,
  step count, error, error screenshot
- structured log rows (timestamp, device, step, status, duration, message,
  error, screenshot) go to `job_logs` in the database and to
  `automation-runs/<date>_<name>_<id>/logs.json`; each device has its own folder
  (`error-stepN.png`, `shot-stepN.png`, `report-stepN.txt`)
- the run directory also contains the exact `workflow.json` that ran

## Image matching, OCR, UI hierarchy

- **ImageMatcher**: normalised cross-correlation on grayscale, coarse search on a
  ≤ 360 px level then full-resolution refinement, non-maximum suppression for
  multiple matches, optional region. No external library; if the project is
  configured with OpenCV (`FARM_HAVE_OPENCV`) the same API can use it.
- **OcrProvider**: the Windows built-in OCR engine (`Windows.Media.Ocr`, C++/WinRT)
  — free, local, already on Windows 10/11 (add a language with *Basic typing*
  under Settings › Time & Language if `available()` is false). Results carry
  word bounding boxes, so *Tap text* and *Wait for text* work without templates.
- **UiHierarchy**: `uiautomator dump --compressed` parsed into nodes with
  `text`, `content-desc`, `resource-id`, `class`, `bounds`, `clickable`,
  `enabled`; selectors like `text=OK; resourceId=android:id/button1; instance=1`.

## Macro recorder

Devices › host mode › *Record macro* (or Automations › *Record macro*).
Taps, long presses, swipes (as screen fractions), typed text (merged), keys and
navigation buttons become nodes; pauses ≥ 300 ms become `time.wait` nodes. The
result opens in the editor for review before it is saved.

## Natural language (optional)

Settings › Automation › Provider: **None** (default), **Ollama (localhost)** or
an **OpenAI-compatible endpoint**. The prompt embeds the node catalog and asks
for JSON only; the answer is validated, risky steps are listed, and nothing runs
until the operator presses *Run*. The application never requires an AI service.

## Import / export

- JSON export/import of a single workflow
- *Export package…* writes a folder `workflow.json + assets/ + README.md`; on
  import, assets referenced by name are resolved from the `assets/` folder next
  to the JSON

## Command line

```
QtScrcpy.exe --run-workflow "Name" [--targets 192.168.100.13:5555,group:Box 1]
```
