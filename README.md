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

## Status

There is no official build/distribution story in the project yet, and for the time being we do not present it as something "ready to build" from the README.

That said, it is straightforward to build yourself if you want to work with it now.
