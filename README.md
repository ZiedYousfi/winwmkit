# WinWMKit

WinWMKit is a C library for building window managers and window-management tools on Windows.

It is not meant to be a framework. It does not try to define your architecture, own your main loop, or force a specific style of app. It is a batteries-included library that gives you the pieces a window manager usually needs.

From the public API, that means:

- top-level window enumeration
- monitor enumeration
- window geometry and monitor geometry helpers
- move, resize, and set-rect operations for windows
- monitor selection helpers for a given window
- an asynchronous event loop with callbacks
- optional named-pipe intake for external commands or integrations

In practice, it is the kind of library you use to make a window manager, not a window-manager framework.

See [`exemple/`](./exemple/) for the current example program using the public API.

## Build

Run them from a Developer PowerShell or a Visual Studio command prompt so `cl.exe` and `lib.exe` are available.

```powershell
python build.py
python build.py static
python build.py shared --no-sanitizers
python exemple/build.py
python build.py compdb
python build.py clean
```

The default configuration enables the address sanitizer. Pass `--no-sanitizers` to build without it, or `--sanitizers <name>` to pick a different MSVC sanitizer mode.
