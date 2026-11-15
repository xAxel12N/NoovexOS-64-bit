Browser window (kernel side of the web engine).

- browser.c window, tabs, toolbar (includes the files below)
- images.c image pool, JPEG/PNG scratch, base64
- info_pages.c error, console and info pages
- site_permissions.c permission bar, site menu, about pages
- mouse_and_bookmarks.c bookmarks bar and mouse handling
- glue/ network and frame glue between kernel and engine (webglue.c includes the rest)
- webwork.c Web Worker host, websock.c WebSockets
