# Part 13: The web engine, JavaScript and cookies

Since v9.08 the browser is a real layout engine with a full JavaScript runtime. This part describes it: how it is built and linked into the kernel, how the kernel talks to it, the HTML parser and DOM, the CSS cascade, layout (block, inline, floats, positioning, tables, flexbox and grid), painting and fonts, the QuickJS bindings and the JavaScript prelude, networking and the cookie jar, the browser window around it, the `qjs` command, and how to test and extend it.

Paths are relative to `source/kernel/`. The engine lives in `web/`; the kernel side is `apps/webglue.c`, `apps/browser.c` and `net/cookies.c`.

| File | Lines | Contents |
|---|---|---|
| `include/nvx/apps/nwe_api.h` | 77 | The only interface between kernel and engine: op codes, `nwe_req`, `nwk_resp`, the `nwk_*` callbacks |
| `web/nwe.h` | 314 | Engine types: `node`, `style`, `box`, `frag`, images, the global document `G` |
| `web/krt.c` | 386 | Freestanding C runtime for the engine and QuickJS: arena `malloc`, `printf` family, `strtod`, time, stubs |
| `web/util.c` | 246 | Pools, string buffers, UTF-8, URL resolution, merge sort |
| `web/dom.c` | 659 | Tag table, nodes and attributes, the HTML parser, entities, serializer |
| `web/css.h`, `web/cssparse.c` | 66, 876 | Selector structures and matching, style sheet parser, `@media` and friends, rule buckets |
| `web/cssstyle.c` | 1803 | Property values, the cascade, inheritance, `var()`, `calc()`, shorthands, the UA style sheet |
| `web/layout.h`, `web/layout.c` | 29, 1539 | Box tree, block and inline formatting, floats, positioning, hit testing |
| `web/layout2.c` | 800 | Intrinsic sizes, flexbox, grid, tables |
| `web/paint.c` | 836 | Painting, stacking, clipping, borders, shadows, gradients, form controls |
| `web/svg.c` | 444 | Inline and image SVG |
| `web/font.c`, `web/fontdata.c` | 275, 31 (generated, about 850 KB of source) | TrueType outline rasterizer and the embedded Liberation fonts |
| `web/js.c` | 1274 | QuickJS runtime, DOM bindings, timers, module loader |
| `web/prelude.js` (to `web/prelude_gen.c`) | 895 | The browser API written in JavaScript on top of the bindings |
| `web/engine.c` | 1209 | Images, scripts, navigation, forms, focus and editing, events, `nwe_entry` |
| `apps/webglue.c` | 289 | Engine stack, HTTP client `nwk_fetch`, the `nwk_*` callbacks, console ring |
| `net/cookies.c` | 454 | Cookie jar and `localStorage`/`sessionStorage`, both saved to disk |
| `apps/browser.c` | 698 | The Browser window: tabs, toolbar, about: pages, input routing, image decoding for the engine |
| `third_party/quickjs/` | | QuickJS-ng, unmodified |
| `tools/mkwebfont.py`, `tools/js2c.py`, `tools/webhost.c` | | Font converter, prelude embedder, host test harness |

---

## 13.1 Build and link

`build.sh` has a `[web]` step that runs before the Doom step:

1. `tools/js2c.py web/prelude.js web/prelude_gen.c nwe_prelude` turns the prelude into a C string.
2. The QuickJS sources (`quickjs.c`, `libregexp.c`, `libunicode.c`, `dtoa.c` and friends) are compiled once into `build/web/` and reused while they are unchanged.
3. The double and float functions of musl's `libm` are compiled into `libmk.a` (the `long double` variants ending in `l` are skipped).
4. The engine files are compiled with the `WEBF` flags: freestanding, `-O2`, SSE allowed, musl headers for the C library declarations.
5. Everything is combined with `ld -r` into one object, and `objcopy --keep-global-symbol=nwe_entry` hides every other symbol. The result is `build/web_all.o` (about 2.4 MB). The step prints any symbol that is still undefined, so a missing runtime function shows up at build time.

Because only `nwe_entry` is exported, the engine cannot clash with kernel names (QuickJS defines `free`, `malloc`, `printf` and many others) and the kernel can only reach it through one door.

The kernel image grew from about 4.3 MB to about 6.7 MB. That needed a new memory layout (section 13.12).

## 13.2 The kernel interface (`nwe_api.h`)

The kernel calls `long nwe_entry(int op, nwe_req* r)`. The ops are:

| Op | Value | What it does |
|---|---|---|
| `NWE_OP_INIT` | 1 | One-time setup: arena, fonts, QuickJS runtime |
| `NWE_OP_LOAD` | 2 | Parse and load a document (`url`, `data`, `len`, viewport `w`/`h`; `btn=1` means plain text) |
| `NWE_OP_LAYOUT` | 3 | Re-layout for a new viewport size |
| `NWE_OP_PAINT` | 4 | Paint into a framebuffer rectangle (`fb`, `stride`, `fbw`, `fbh`, `x`, `y`, `w`, `h`) |
| `NWE_OP_SCROLL` | 5 | Scroll by `y` pixels |
| `NWE_OP_MOVE` | 6 | Mouse moved (hover, cursor shape in `cursor`, link in `href`) |
| `NWE_OP_DOWN`, `NWE_OP_UP` | 7, 8 | Mouse button; a click is a down and an up |
| `NWE_OP_KEY` | 9 | Key press (`key`; arrows and editing keys are `NWE_K_UP` = 19 ... `NWE_K_END` = 26) |
| `NWE_OP_TICK` | 10 | Run due timers, animation frames, network callbacks, pending images |
| `NWE_OP_FIND` | 11 | Find in page |
| `NWE_OP_EVAL` | 12 | Evaluate a JavaScript expression; the result is in `out` |
| `NWE_OP_INFO` | 13 | Fill in title, scroll position and document height |
| `NWE_OP_WHEEL` | 14 | Mouse wheel at `x`,`y` by `key` pixels; scrolls the innermost scrollable box first |

Every op returns the page title, scroll position, document height (`doch`), viewport height and a `dirty` flag. When script or a click wants to navigate, `nav`, `navmethod`, `navbody` and `navblen` are set and the kernel performs the request (the engine never fetches a top-level page itself).

The engine calls back into the kernel only through `nwk_*` functions: `nwk_fetch`, `nwk_free`, `nwk_image` (decode PNG, JPEG, WebP, GIF, BMP or ICO to ARGB), `nwk_cookie_get`/`nwk_cookie_set`, the `nwk_store_*` storage functions, `nwk_log`, `nwk_status`, `nwk_alert`, `nwk_ms`, `nwk_epoch_ms`, `nwk_tz_min`, `nwk_pump` (keeps the network stack running during long fetches) and `nwk_alloc_arena`.

### The engine stack

`web_call(op, r)` in `apps/webglue.c` runs every op on a separate 2 MiB stack through the assembly trampoline `nwe_call_stack` (`arch/x86_64/stack_call.c`). QuickJS and the recursive layout code need more stack than the kernel's boot stack can spare. A guard (`g_in_engine`) refuses re-entry: if a callback such as a fetch pumps the GUI and something tries to call the engine again, `web_busy()` is true and the browser draws "Loading..." instead.

### Failure handling

`nwe_entry` sets a recovery point with `__builtin_setjmp`. `abort()`, an arena that is completely full, or an internal check calls `nwe_fail`, which jumps back and makes the op return -1. The browser then shows "This page stopped working (engine error)" and the next load starts from a clean arena. The rest of the system is not affected.

## 13.3 Memory

All engine and JavaScript memory comes from one arena that `nwk_alloc_arena` takes from the kernel heap at INIT. It tries 160 MiB, then 96, 64 and 32 MiB. `web/krt.c` implements `malloc`, `free`, `realloc`, `calloc` and `malloc_usable_size` on it: size classes from 16 bytes to 60,592 bytes with free lists, and a list for larger blocks (page-rounded, reused when a free block is at most twice the request). Before v9.09 requests between the largest class and 64 KiB were given the largest class and overflowed it.

The whole arena is reset on every page load, so nothing a page allocates can leak into the next one.

Within a page, three scratch pools are reset on every pass:

| Pool (`g_pool`) | Reset by | Holds |
|---|---|---|
| 1 | each restyle (`style_all`) | computed styles |
| 2 | each layout (`layout_all`) | boxes, line fragments |
| 3 | each paint (`paint_all`) | paint lists and temporary buffers |

`nfree` ignores pointers that belong to a pool. Anything that must survive a pass (font and glyph caches, decoded images, parsed inline `style` declarations, rebuilt style sheets) is allocated between `pool_suspend()` and `pool_resume()`.

QuickJS gets a memory limit of 60% of the arena (`JS_SetMemoryLimit`) and an 800 KiB stack limit. A page that runs out gets a JavaScript `InternalError`, not a crash.

## 13.4 HTML parsing and the DOM (`web/dom.c`)

The parser is a single-pass tokenizer with the tree-building rules that real pages rely on:

- implied `html`, `head` and `body` elements, and implied end tags (`p`, `li`, `dt`/`dd`, `option`, table parts);
- foster parenting of stray content in tables;
- raw text for `script`, `style`, `textarea` and `title`;
- named and numeric character references;
- `document.write` during parsing (output goes into `G.wbuf` and is parsed in place).

Nodes (`node` in `web/nwe.h`) carry the tag, attributes, children, a pointer to the JavaScript wrapper (`jsobj`), form state (`value`, `caret`, `checked`, `selidx`), a decoded image and the scroll offset of scrollable boxes. `parse_fragment` (used by `innerHTML`, `insertAdjacentHTML` and `outerHTML`) parses into a holder element of the right context, so table fragments work. `serialize` and `clone_node` implement `innerHTML`/`outerHTML` reads and `cloneNode`.

## 13.5 CSS (`web/cssparse.c`, `web/cssstyle.c`)

### Selectors and rules

- Compound selectors with type, `#id`, `.class` and `*`; the combinators descendant, `>`, `+` and `~`.
- Attribute selectors: `[a]`, `=`, `~=`, `|=`, `^=`, `$=`, `*=`, with the `i` flag.
- Pseudo-classes: `:hover`, `:focus`, `:focus-within`, `:active`, `:checked`, `:disabled`, `:enabled`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`, `:nth-last-child()`, `:nth-of-type()`, `:first-of-type`, `:last-of-type`, `:empty`, `:root`, `:link`, `:visited`, `:not()`, `:is()`, `:where()`, `:has()`, and more. `::before` and `::after` generate content; `::marker` and `::placeholder` are styled.
- At-rules: `@media` (width, height, orientation, `prefers-color-scheme` (light), and the usual `and`/`not`/`,` logic), `@supports`, `@import`, `@layer`, `@container` (treated as matching), `@font-face` and `@keyframes` (parsed and skipped).

Nested rules are flattened while parsing (`parse_body` and `nest_combine` in `web/cssparse.c`): each `&` is replaced by the parent selector, a child selector without `&` becomes a descendant of the parent, and nested `@media`, `@supports`, `@layer` and `@container` blocks apply to the parent selector.

Rules are put into buckets by the rightmost compound's id, class or tag, plus a universal bucket, so matching a node only looks at rules that can apply.

### The cascade

`compute()` applies declarations in this order: the UA style sheet (`css_ua_text`), presentational attributes (`width`, `bgcolor`, `align` and so on), author rules by specificity and order, inline `style`, then `!important`. Inherited properties come from the parent's computed style. There are 177 properties in the `PROPS` X-macro in `web/css.h`; custom properties (`--x`, id `P_CUSTOM` = 1000) are stored per element and resolved by `var()` with fallbacks.

Lengths are stored as `L {px, pct, lo, hi, k}`, which can represent `calc()`, `min()`, `max()` and `clamp()` with mixed units; `lres()` resolves one against a reference size. Units: `px`, `em`, `rem`, `%`, `vw`, `vh`, `vmin`, `vmax`, `pt`, `pc`, `in`, `cm`, `mm`, `ch`, `ex`.

Values also cover colours (named, hex with 3/4/6/8 digits, `rgb()`/`hsl()`/`hwb()` in both syntaxes, `oklch()`, `oklab()`, `lab()`, `lch()`, `color(srgb ...)`, `light-dark()` (light side), `color-mix()`, system colours such as `Canvas` and `CanvasText`, `currentColor`, `transparent`), linear, radial and conic gradients, multiple box shadows, text shadows, `transform` (translate, scale, rotate is ignored, matrix), `opacity`, `filter` (ignored), and the shorthands `margin`, `padding`, `border*`, `background`, `font`, `flex`, `flex-flow`, `grid-template`, `grid-area`, `gap`, `inset`, `list-style`, `overflow`, `place-*`.

## 13.6 Layout (`web/layout.c`, `web/layout2.c`)

`layout_all` builds a box tree from the styled DOM (with anonymous block and inline wrappers where needed) and lays it out:

| Feature | Notes |
|---|---|
| Block flow | `box-sizing`, min/max sizes, auto margins, margin collapsing between siblings and through parents (first and last child), `aspect-ratio` |
| Inline flow | text runs and inline boxes are broken into lines; `white-space` (normal, nowrap, pre, pre-wrap, pre-line), `word-break`/`overflow-wrap`, `text-align` including `justify`, `vertical-align`, `line-height`, `letter-spacing`, `text-indent`, `text-transform`; inline-block and replaced elements sit on the baseline |
| Floats | left and right floats in a block formatting context, text flows around them, `clear` |
| Positioning | `relative` (and `transform` translate) offsets after layout; `absolute` and `fixed` boxes are laid out after the main pass against their containing block; `sticky` with `top` sticks while its containing block is in view (applied at paint time) |
| Stacking | `z-index` and positioned elements are painted in stacking order |
| Overflow | `hidden`, `scroll` and `auto` clip; scrollable boxes keep their own scroll offset and take the wheel |
| Tables | an occupancy grid with `colspan` and `rowspan`, automatic column widths from min/max content, `border-collapse`, `border-spacing`, captions |
| Flexbox | the full algorithm: direction, wrap, grow/shrink/basis, `order`, `justify-content`, `align-items`/`align-self`/`align-content`, `gap`, auto margins |
| Grid | `grid-template-columns`/`rows` with px, %, `fr`, `auto`, `minmax()`, `repeat()` including `auto-fill`/`auto-fit`; line placement, `span`, named areas, auto placement, `gap`, alignment |
| Text overflow | `text-overflow: ellipsis` on overflow-hidden boxes, and `-webkit-line-clamp` (with `display: -webkit-box`) cuts the box after N lines and ends the last line with an ellipsis |
| Lists | `list-style-type` (disc, circle, square, decimal, roman, alpha, none) and `list-style-position` |
| Forms | `input` (text, password, checkbox, radio, range, color, number, search, email, submit, button, reset, file, hidden), `textarea`, `select`, `button`, `progress`, `meter` |

`intrinsic()` gives min-content and max-content widths for shrink-to-fit boxes, table columns and flex bases. `hit_test` maps a point to the deepest node for mouse events.

The engine paints only what the window shows: layout produces the document height (`doch`), and the kernel scrolls the viewport.

## 13.7 Painting, fonts and images

`paint_all` walks the boxes in stacking order and draws backgrounds (colours, gradients, images with `background-size`, `-position` and `-repeat`), borders (solid, dashed, dotted, double, inset, outset; rounded corners with anti-aliasing via `rr_cov`), box shadows, text with decorations and shadows, list markers, form controls, the focus ring, the open `select` list, and find-in-page highlights. Clipping follows `overflow` and the viewport; fixed elements use the viewport clip. Boxes hidden with `clip: rect(...)` of zero area, `clip-path: inset(50%)` or a 1-px overflow-hidden box are skipped. `-webkit-background-clip: text` paints the text in the gradient's first colour instead of painting the background.

Fonts. `tools/mkwebfont.py` converts Liberation Sans, Serif and Mono (regular, bold, italic) into outline data in `web/fontdata.c`. Composite glyphs are decomposed, and glyphs are sorted by code point for binary search. Sans and Sans Bold also carry Greek, Cyrillic, Latin Extended-A and about 80 symbols taken from DejaVu Sans (check marks, arrows, stars, card suits, the hamburger menu, warning sign, gear, envelope and so on); other faces fall back to them, and a code point that no face has is drawn as `?`. `web/font.c` rasterizes outlines with an accumulation buffer (exact area coverage, as in font-rs) and caches glyphs by (face, size in quarter pixels, code point). `font-family` maps generic and common names (Arial, Helvetica, system-ui, Georgia, Times, Courier, monospace ...) to the three families.

Images. `<img>`, `<picture>`/`srcset` (the best candidate for the viewport), CSS background images and `data:` URLs are fetched and decoded by the kernel (`nwk_image`) and scaled with bilinear filtering. SVG files and inline `<svg>` are rendered by `web/svg.c`: paths (all commands including arcs), basic shapes, `use`/`symbol`, `viewBox`, transforms, fill and stroke. `loading="lazy"` images load when they come near the viewport.

## 13.8 JavaScript (`web/js.c`, `web/prelude.js`)

The JavaScript engine is QuickJS-ng (ES2023: classes, modules, async/await, generators, Proxy, BigInt, typed arrays, regular expressions with Unicode). Each page gets a fresh runtime.

Two layers implement the browser API:

1. C bindings in `web/js.c`. DOM nodes are objects of one native class that holds a pointer to the `node`. `node_funcs`, `elem_funcs` and `doc_funcs` provide tree access and mutation, attributes, `textContent`, `innerHTML`/`outerHTML`, `value`, `checked`, geometry (`getBoundingClientRect`, `offset*`, `client*`, `scroll*`), `querySelector`/`querySelectorAll`/`matches`/`closest`, `getComputedStyle`, focus, and form submission. Internal helpers sit on a hidden `__n` object.
2. The prelude, about 900 lines of JavaScript that run before the page. It defines `EventTarget`, `Event` and its subclasses, the event dispatcher (capture, target and bubble phases, `once`, `passive`, `stopPropagation`, `preventDefault`, inline `on...` attributes), `DOMTokenList` (`classList`), `dataset`, the `style` object (backed by inline declarations), the HTML element constructors and prototypes (`HTMLInputElement` and about 60 others, set per tag with `N.setProto`), `window`, `location`, `history` (`pushState`, `replaceState`, `popstate`), `navigator`, `screen`, `localStorage`/`sessionStorage`, `document.cookie`, `fetch` with `Response`/`Headers`, `XMLHttpRequest`, `URL`/`URLSearchParams`, `FormData`, `Blob`, `TextEncoder`/`TextDecoder`, `atob`/`btoa`, `MutationObserver`, `IntersectionObserver`, `ResizeObserver`, `customElements`, `matchMedia`, `requestAnimationFrame`, `queueMicrotask`, `structuredClone`, `performance.now`, and `console`.

Editing. Elements with `contenteditable` (and `plaintext-only`) are focusable. A click places the caret at the clicked character (`ce_place` in `web/engine.c`), and each key goes to `__ceKey` in the prelude, which fires a cancelable `beforeinput` with `inputType` and `data`, edits the DOM (insert text, delete backward or forward and merge blocks, split the paragraph on Enter), moves the selection and fires `input`. `Selection` and `Range` are full objects (anchor, focus, direction, `setBaseAndExtent`, `extend`, `modify`, `deleteContents`, `insertNode`, `getBoundingClientRect` through the native `caretRect`). The engine draws the caret from the selection's focus (`caret_pos` in `web/layout.c`). `MutationObserver` is real: `web/dom.c` calls a hook for every child, attribute and text change while an observer exists (never during parsing), and records are delivered in a microtask. This is what editors like ProseMirror, which ChatGPT uses, rely on.

Streams. `ReadableStream` (readers, async iteration, `pipeThrough`, `pipeTo`, `tee`), `WritableStream`, `TransformStream`, `TextDecoderStream` and `TextEncoderStream` are implemented in the prelude. `Response.body` is a stream holding the downloaded body, and `new Response(stream)` reads a stream. Responses still arrive complete (section 13.15), so a streamed answer appears all at once.

Every element, the document and window have real `on...` properties for about 110 events (`onclick`, `oninput`, `onload` ...), and `HTMLInputElement.prototype` (and the textarea, select and option prototypes) own the `value`/`checked` accessors. Libraries test both: React checks `'oninput' in document` and installs its value tracker from the prototype's `value` descriptor.

Scripts. Classic scripts run in document order; `defer` and `type="module"` scripts run after parsing; `async` scripts run when they arrive. Modules are loaded through a QuickJS module loader that fetches `import`ed URLs. Scripts inserted by script (`createElement('script')`) run and fire `load` or `error`. `DOMContentLoaded` and `load` fire in the right order.

Timers and the event loop. `setTimeout`, `setInterval` and `requestAnimationFrame` are driven by `NWE_OP_TICK`, which the browser sends about every 20 ms while its window is open. Microtasks (promises) run after every script and every event.

Limits. A script that runs longer than 6 seconds without returning is interrupted (the interrupt handler compares `nwk_ms()` with a deadline that is armed before each entry into JavaScript). Uncaught errors go to the console (`about:console`) with a stack trace.

Redrawing. A DOM or style change only marks the document dirty (`dirty_style`, `dirty_layout`, `dirty_paint`); restyle and layout happen once, before the next paint or the next geometry query from script.

Tested with: React 19.2 (bundled todo app, state, effects, controlled inputs), jQuery 3.6 (DOM building, `.css`, `.addClass`, `fadeIn`), and the 290 KB Go language specification (laid out in about 57 ms on the host).

Binary data (v9.12): `N.fetch(url, method, body, ctype, bin)` returns the body as an `ArrayBuffer` when `bin` is true, and the body argument may be a string or any `ArrayBuffer`/typed array/`DataView` (read with `jbytes` in `js.c`). `fetch` always asks for bytes; `Response` keeps them in `_u8` and decodes text only for `text()`/`json()`. `XMLHttpRequest` asks for bytes when `responseType` is `arraybuffer` or `blob`. `Blob` keeps either a string or bytes. `TextDecoder` (UTF-8) and `TextEncoder` call the natives `utf8dec` and `utf8enc` instead of looping in JavaScript.

## 13.9 Networking and the HTTP client (`apps/webglue.c`)

`nwk_fetch(url, method, ctype, body, blen, referer, resp)` is the only HTTP client the engine uses:

- HTTP/1.1 with `Connection: close`: plain HTTP over the kernel TCP stream (`tcps_*`), HTTPS over the kernel TLS 1.3 client (`tls_get`) with certificate checks;
- up to 7 redirects (301, 302, 303, 307, 308; a 303, or a POST answered with 301/302, continues as GET);
- chunked transfer and `gzip` content encoding (`Accept-Encoding: gzip`; a `deflate` body is also decoded);
- `Cookie` headers from the jar and `Set-Cookie` back into it, on every request including subresources and `fetch()`;
- a Chrome-like `User-Agent` (`WEB_UA`, ends in `Noovex/9.08`) so sites send their normal pages;
- `Referer`, `Accept` and `Accept-Language: en-US` headers.

Typed addresses without a scheme try HTTPS first and fall back to HTTP.

## 13.10 Cookies and web storage (`net/cookies.c`)

The jar holds up to 400 cookies (`CK_MAX`). For each cookie it keeps name, value (511 bytes), domain, path, expiry, creation time and the flags secure, HttpOnly, host-only and SameSite.

| Rule | Implementation |
|---|---|
| Domain | Without `Domain` the cookie is host-only. `Domain` must domain-match the request host and contain a dot (or be `localhost`); a leading dot is ignored |
| Path | Default path is the request path up to its last `/`; matching follows RFC 6265 |
| Expiry | `Max-Age` wins over `Expires`; `Max-Age=0` or a date in the past deletes the cookie. Expired cookies are dropped when they are next looked at |
| Secure | Only sent over HTTPS, and only set from HTTPS |
| HttpOnly | Sent with requests but hidden from `document.cookie` (the `forjs` flag), and JavaScript cannot overwrite it |
| SameSite | Parsed, stored and shown in about:cookies. There is no cross-site request context yet, so it does not change what is sent |
| Order | Longer paths first, as browsers do |

Persistence. Cookies with an expiry are saved to `cookies.txt` in the user's file system (`fs_save_bin`), at most every 3 seconds while something changed. Session cookies live until reboot. `localStorage` (keyed by origin; 600 entries in total) is saved to `webstorage.txt` the same way; `sessionStorage` entries are kept in memory only.

The old `ck_*` functions in `net/http.c` (used by the `http` command and the account code) are now wrappers around the same jar.

## 13.11 The Browser window (`apps/browser.c`)

The window keeps the toolbar from earlier versions (back, reload, bookmark star, address bar, Go, Save, Home, Find, and a new Cookies button) and up to 6 tabs. Switching tabs reloads the page of that tab.

| Page | Contents |
|---|---|
| `about:blank` | empty page |
| `about:js` | a JavaScript demo page |
| `about:cookies` | every stored cookie by site with its flags; buttons to clear everything (`?clear`) or remove one site (`?del=`) |
| `about:console` | the last console messages and script errors |
| `about:history`, `about:bookmarks` | history and bookmarks |

Downloads (`Content-Disposition: attachment` or a file extension like `.zip`) go to Downloads, and a direct link to an image opens the image viewer. Network errors show an error page with the reason (DNS, connect, timeout, TLS).

Input. Mouse moves send `MOVE` (hover styles, link cursor, status-bar URL); a click sends `DOWN` and `UP`; the wheel goes to `browser_wheel`; keys are mapped by `map_key` (the kernel's arrow, Home, End, Page and Delete codes become `NWE_K_*`). `browser_tick` sends `TICK` from the main loop (`gui/input.c`) and asks for a redraw when the page changed.

Shell. `browse <url>` (or `www <url>`) opens the browser at a URL; in the live terminal it first switches to the desktop. `desktop` switches from the live terminal to the desktop without opening anything.

## 13.12 Memory layout change in v9.08

Note: v9.11 changed the layout again (15 MiB image, stack at `0x1080000`, `.bss` at `0x1300000`); section 2.7 has the current map. This section records why v9.08 had to move the stack.

The 6.7 MB kernel no longer fit below the boot stack at `0x700000`. Nothing reported it: the stack simply overwrote the end of `.rodata`, and the boot integrity check failed ("kernel code was modified"). The layout is now:

| Range | Use |
|---|---|
| `0x100000-0x900000` | kernel image, at most 8 MiB (`ASSERT(__file_end <= 0x00900000)` in both linker scripts) |
| `0x900000-0x980000` | boot stack, top at `0x980000` (`boot64.S`, both entry paths) |
| `0x980000-0xC00000` | USB and disk buffers (xHCI from `0x990000`) |

The UEFI loader's `KERNEL_MAX` is now `0x900000-0x100000` (8 MiB), and its kernel-area table, the installer's memory test and the docs use the same boundaries. The ChaCha20-Poly1305 code used a fixed scratch buffer at `0x900000`, which is now inside the stack area; it uses a heap buffer instead (`aead_mac` in `lib/crypto/nvxcrypto.c`).

## 13.13 The `qjs` command

`qjs` is QuickJS-ng's own command-line interpreter, built as a musl program (with `std` and `os` modules, `gen/repl.c` and `gen/standalone.c`), compressed and embedded like the other ports (section 6.6). Examples:

```
qjs -e "print(1+2, [1,2,3].map(x=>x*x).join(','))"
qjs script.js
qjs                      (interactive REPL)
```

It is separate from the browser's copy of QuickJS: it runs as a user process with the Linux system-call layer, while the browser's copy runs inside the kernel on the engine stack.

## 13.14 Testing the engine

Host harness. `tools/webhost.c` links the engine objects (built for Linux with `-D_GNU_SOURCE`, without `krt.c`) and renders a file to a PPM image:

```
webhost page.html out.ppm [width] [height] [maxheight] [javascript-to-eval]
```

It prints load and paint times, the title, the document height, every fetch and every console message. The optional last argument is evaluated after the page has loaded and its result is printed, which makes it easy to click buttons or read the DOM from a test. Link it with `-lm -lz` (zlib provides `nwk_inflate` for WOFF fonts). With `WH_MS=N` in the environment it keeps ticking the engine in real time for N milliseconds after loading, so timers, animations, transitions and the simulated media backend run before the picture is taken.

In QEMU. The test site used for v9.08 runs a small HTTP server on a tap device that answers every DNS name (so `browse noovex.test` works) and has pages for cookies (`/cookie/set`, `/cookie/show`), a form echo (`/echo`), a JSON API (`/api/data`) and a redirect. Checked in QEMU: rendering, `fetch()`, images, clicks, form POST, Back, server cookies (including HttpOnly hidden from script), about:cookies, a React app with real keyboard input, and jQuery.

## 13.15 Known gaps

- No `<canvas>` drawing, `<iframe>` content, Web Workers, WebSockets or WebAssembly. `<video>` has no subtitles, no encrypted media (EME) and plays at speed 1 only (section 13.17).
- Web fonts with CFF outlines (`.otf` files that are not TrueType inside) are not used; only ligature substitution is taken from `GSUB`, and there is no kerning (`GPOS`).
- 3D transforms are flattened to their 2D part, and transformed elements are hit-tested at their translated position only. Animations of layout properties relayout the page on every frame, which is slow on large pages.
- Gradient text is drawn in one colour, not as a gradient.
- Editing has no text selection by mouse drag, no clipboard and no undo; Shift and Ctrl combinations are not passed to pages.
- One HTTP connection per request (no keep-alive, no HTTP/2), and requests are synchronous, so a slow server stalls the browser until it answers or times out. Fonts and stylesheets are also fetched synchronously when they are first needed.
- Shadow DOM: styles are scoped by the host's tag name, so two different shadow roots on the same kind of element share their styles; `:host-context()`, `slotchange` and closed-mode retargeting are not implemented.
- SameSite is not enforced, and there is no public-suffix list (a cookie for `co.uk` would be accepted).
- Right-to-left text, complex scripts and emoji are not shaped.

## 13.16 Extending the engine

- A new CSS property: add it to `PROPS` in `web/css.h`, parse it in `apply()` in `web/cssstyle.c`, add a field to `style` in `web/nwe.h`, and use it in layout or paint. If it is inherited, add it to the inheritance copy in `compute()`.
- A new DOM method: if it needs engine internals, add a native function to the right table in `web/js.c`; otherwise write it in `web/prelude.js` (it is rebuilt by `build.sh`).
- A new kernel service for the engine: add an `nwk_` function to `nwe_api.h`, implement it in `apps/webglue.c` (and a stub in `tools/webhost.c`), and remember that it runs on the engine stack.
- After any change, render a few pages with `webhost` before booting: it is much faster than QEMU and errors print to the terminal.

## 13.17 Video and audio in the browser

Every `<video>` or `<audio>` that has a source gets a media slot (section 6.13). The pieces:

| Piece | Where | What it does |
|---|---|---|
| `N.mediaOp(op, el, ...)` | `web/js.c`, native 36 | 0 create (URL or empty for kernel data, split flag, start ms, paused), 1 command, 2 status as `[cur_ms, dur_ms, flags, w, h, buffered%]`, 3 append bytes to stream 0 or 1, 4 end of a stream, 5 restart at a time, 6 free, 7 stream length |
| registry | `web/wmedia.c` | up to 8 elements with their slot, the rectangle and clip they were last painted at, the last frame sequence, and the video size |
| painting | `wm_paint` from `paint.c` | draws the frame with `object-fit: contain` and black bars, a waiting shade, a big play button before the first play, and, with the `controls` attribute, a bar with play/pause, time and a seek line (an `<audio controls>` is a 300 x 54 bar). A size change sends `SIZE` so the decoder scales to the element |
| fast path | `NWE_OP_MEDIA`, `wm_fast` | when nothing else on the page is dirty, only elements with a new frame are redrawn into the window's part of `SCENE`; the browser copies that rectangle to the screen (`browser_media_fast`). Anything painted over a video is restored at the next full paint |
| tick | `wm_tick` in `NWE_OP_TICK` | asks for a repaint when a status flag changes, every second while controls are shown, and when the first frame arrives; when the video size becomes known it asks for a new layout, because `replaced_size` uses it as the intrinsic size |
| clicks | `wm_click` in `NWE_OP_UP` | a click on a video with controls calls `__mediaUI`: toggle play, or seek to the clicked point of the bar; it runs after the page's own click handlers and only if they did not cancel the event |

The element API is in `web/prelude.js` (search for `class TimeRanges`). State lives in a hidden `__ms` object. `load()` picks `src` or the first playable `<source>`; a `blob:` URL is looked up in the table that `URL.createObjectURL` now keeps, so a `MediaSource` attaches and a `Blob` is handed over as kernel data; `data:` URLs are decoded; anything else is resolved and passed as a URL, which `play` downloads itself (no cookies are sent). One 100 ms interval polls every active element and turns status changes into events: `durationchange`, `loadedmetadata`, `loadeddata`, `canplay`, `canplaythrough`, `playing`, `waiting`, `timeupdate` (every 250 ms), `seeked`, `ended`, `error`, `resize`. A 700 ms scan picks up elements whose `src` was set as an attribute, and starts `autoplay`.

MediaSource: `addSourceBuffer` allows two buffers. The decoder starts when every buffer has received its init segment (or after a few appends); stream 0 is the first buffer. `appendBuffer` is asynchronous like in browsers (`updatestart`, `update`, `updateend`). Each append is scanned for timestamps so `buffered` is right: for MP4, `mdhd` timescale and `trex` default duration from the init segment, then `tfdt` and the `trun` sample durations of each `moof`; for WebM, `TimecodeScale` and `Duration` from `Info`, then each `Cluster` time code plus the largest block offset. A seek inside the buffered ranges restarts the decoder with all data kept; a seek outside drops everything but the init segments, and the page then appends segments for the new time. `isTypeSupported` and `canPlayType` accept MP4, WebM, Ogg, MP3, WAV and AAC with the codecs FFmpeg was built with, and refuse AV1, high-bit-depth VP9 profiles, AC-3/E-AC-3, more than 2 channels, and video larger than 1920 x 1088 or faster than 60 fps, so adaptive players choose a stream the decoder can handle.

Full screen: `requestFullscreen` gives the element a fixed, full-viewport style (the old style is saved), sets `document.fullscreenElement` and fires `fullscreenchange`; `exitFullscreen` restores it.

Testing: `tools/webhost.c` has a simulated media backend (a moving clock, a test pattern frame, and a log of every command and append), so the element API and MSE can be tested on the host with `WH_MS=1500 webhost page.html out.ppm`; `WH_MS` keeps ticking the engine in real time for that many milliseconds. In QEMU, a page with a progressive MP4 and a page that feeds fragmented MP4 video and audio through MediaSource both play to the end.

YouTube itself was not tested: the build machine cannot reach it, and it may need more (for example its own streaming protocol, signature code and checks that the browser is real). Reports of what the browser log shows on youtube.com help to close the gaps.

## 13.18 Transforms, animations and transitions

Transforms. `transform` is parsed into a list of functions (`tfn`: translate with lengths, scale, rotate, skew, matrix; `matrix3d` and `rotate3d` keep their 2D part) stored on the style, together with the `translate`, `rotate` and `scale` properties and `transform-origin`. `tf_matrix(style, w, h, m)` multiplies them into one 2D matrix `x' = a x + c y + e, y' = b x + d y + f` about the origin. Layout moves a transformed box by `e, f` (stored as `box.btx/bty`), so hit-testing and `getBoundingClientRect` follow translations. A transformed element is a stacking context. In `paint_stack`, an element whose matrix has a non-identity linear part (or that is animated) goes through `paint_transformed`: the bounds of its subtree are measured (`sub_bounds`, at most 1800 px around the origin and 1.6 million pixels), the subtree is painted twice into off-screen buffers, once over black and once over white, and the two results give colour and coverage for every pixel. That buffer is then drawn through the inverse matrix with bilinear filtering. No primitive needs to know about alpha. A video inside a transformed element is drawn but not registered for the fast frame path. A translation-only change (a sliding element) is painted with an offset and needs no buffer.

Animations and transitions live in `web/anim.c`.

| Piece | What it does |
|---|---|
| `@keyframes` | parsed in `cssparse.c` (`parse_keyframes`; `from`, `to`, percentages, lists) into `kfset` records; cleared when the sheets are rebuilt |
| properties | `animation` and its 8 longhands, `transition` and its 4 longhands, parsed into `animspec`/`transspec` arrays on the style (up to 8 animations and 12 transitions per element) |
| keyframe styles | after each style pass `anim_after_style` links every element that names a known `@keyframes` to a run record (start time kept across style passes, keyed by element and name) and computes one style per keyframe with `css_style_with` (the element's style plus that keyframe's declarations) |
| transitions | `anim_before_style` saves the current value of every transitioned property before a style pass; after it, a changed value starts a transition from the saved value (which may be the middle of a running transition) to the new one |
| values | opacity, colours (`color`, `background-color`, border and outline colours, `fill`, `stroke`), lengths (`width`, `height`, min/max, `top/right/bottom/left`, margins, paddings, border widths, `font-size`, `letter-spacing`), `border-radius`, transforms (function by function when both lists have the same functions, otherwise as matrices) and `visibility` |
| timing | `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`, `cubic-bezier()` (solved with Newton steps), `steps()`, `step-start`, `step-end`; per-keyframe segments, `delay`, `iteration-count` (including `infinite` and fractions), `direction`, `fill-mode`, `play-state` |
| frames | the engine tick calls `anim_tick` at most every 33 ms; it writes the current values straight into the computed styles, asks for a repaint, and for a relayout if a layout property is animating. It then dispatches the queued `animation*` and `transition*` events |

## 13.19 Web fonts

`@font-face` rules are collected by `fontface_add` (`web/webfont.c`): family, weight or weight range, style, `unicode-range` (a face whose range covers Latin letters is preferred) and the `src` list, ranked woff2, woff, truetype, opentype. When `face_for` meets a family that has `@font-face` rules, `wf_face` picks the closest face and loads it the first time (`eng_fetch_text`, which also handles `data:` URLs); a URL that fails is not tried again on that page.

`web/ttf.c` turns the file into a dynamic face (up to 32 per page, numbered after the 9 built-in faces; `nwe_face(face)` returns the record for either kind):

| Format | Handling |
|---|---|
| TTF / TrueType OTF | tables read in place |
| WOFF 1 | every table inflated with the kernel's zlib (`nwk_inflate`) |
| WOFF 2 | `web/brotli.c` decompresses the table stream (RFC 7932, with the static dictionary and transforms in `web/brotli_tab.c`, extracted from the reference library), then `web/woff2.c` rebuilds `glyf`/`loca` from the transformed streams (triplet-coded points, 255UInt16 counts, composite records, bounding-box bitmap) and `hmtx` if transformed, and writes a normal sfnt |

`cmap` formats 4 and 12 give the code points; advances come from `hmtx`, metrics from `head`, `hhea` and `OS/2`. Glyph outlines are decoded from `glyf` only when a glyph is first drawn (`ttf_outline`), including composite glyphs with offsets and scales, into the same op list the built-in faces use, and cached. A missing glyph falls back to the built-in sans face. Ligatures: lookup type 4 (and type 7 extensions) of the `liga`, `rlig` and `clig` features become ligature records; each result glyph gets a private code point from U+F0000. `ttf_shape` replaces matching runs (longest match first) before text is measured and drawn, so a word such as "home" in an icon font becomes one icon.

Tests: the Brotli decoder was checked against the reference encoder on 800 random inputs (all qualities, windows 10 to 24, generic, text and font modes), and WOFF2 files made with fontTools from DejaVu Sans (6,253 glyphs) and Liberation Serif (2,602) rebuild to exactly the original outlines and advances.

## 13.20 Shadow DOM

`node.shadow` points from a host to its shadow root (a fragment) and `node.host` back. `attachShadow` (native 39) creates it; the JavaScript object gets the `ShadowRoot` prototype (`host`, `mode`, `innerHTML`, `getElementById`, `activeElement`, `adoptedStyleSheets`). Closed roots are hidden from `shadowRoot`.

| Area | Behaviour |
|---|---|
| rendering | `build_children` uses the shadow root's children instead of the host's. A `<slot>` inside a shadow tree is replaced by the host's children whose `slot` attribute matches its `name` (the default slot takes the rest, including text), or by its own children when nothing is assigned |
| style | the shadow tree inherits from the host; light-DOM children inherit from the slot they are shown in. Stylesheets inside a shadow root are collected with the host's tag name as their scope: they match only elements inside shadow roots of that tag, `:host` and `:host(selector)` match the host, `::slotted(selector)` matches its assigned children. Page stylesheets do not reach into shadow trees (the browser's default sheet does) |
| events | the event path continues from a shadow root to its host |
| DOM | `getRootNode({composed})`, `assignedNodes()`, `assignedElements()`, `document.adoptedStyleSheets`, `new CSSStyleSheet()` with `replaceSync`/`replace`/`insertRule` |

Insertions into a shadow tree count as document changes (`in_composed_doc`), so they restyle the page.

## 13.21 Style system speed

| Change | Effect |
|---|---|
| Stylesheet cache (`parse_cached`): parsed rules, keyframes and font faces are kept per sheet, keyed by an FNV hash of the text, base URL, media and shadow scope | a rebuild re-uses unchanged sheets instead of parsing them again |
| Incremental index: when the sequence of sheets of the last rebuild is a prefix of the new one, only the new rules are added to the id/class/tag buckets | adding a `<style>` element costs only its own rules |
| Ancestor filter: `style_tree` keeps counts of the tags, ids and classes of the current ancestors (4096 slots); each selector remembers up to 4 hashes of compounds that must be ancestors (descendant or child combinators) and is skipped at once if one is missing | most tag-bucket rules with descendant selectors are rejected without walking up the tree; it is only used during a style pass |
| `@media` results are cached per rule for each style pass | the media query string is no longer parsed for every element |

On the host, a page with 18,000 rules that adds 60 `<style>` elements and forces a style pass after each went from 2,436 ms to 282 ms.

## 13.22 Crash containment

The engine runs on its own 2 MiB stack (`g_estack`, `apps/webglue.c`). When a CPU exception happens in kernel mode, `isr_body` (`arch/x86_64/interrupts.c`) first calls `web_fault_redirect`. If the browser is inside an engine call and the faulting stack pointer is in the engine stack (or just below it, for an overflow), the interrupt frame is changed so that `iretq` lands in `web_fault_land` on a safe part of that stack. `web_fault_land` calls `nwe_fail`, which unwinds to the `setjmp` in `nwe_entry`; the engine marks the page dead and returns -1.

`web_call` then sets `g_web_failed`. `browser_tick` sees it and shows "This page crashed" (`browser_crashed`); the next load resets the engine. NMI, double fault and machine check are never redirected, and after 64 redirected faults the normal error screen is shown again, so a broken engine cannot loop forever.

`about:crash` writes to a non-canonical address inside the engine to test this path.

## 13.23 YouTube player page

Since v9.15 `yt_try` is no longer called, so YouTube addresses load the real site. The description below is kept for the code that is still in `apps/browser.c`.

YouTube's desktop site needs far more JavaScript and layout than the engine handles, so `yt_try` (`apps/browser.c`) recognises watch addresses before any network request: `youtube.com`, `www.`, `m.`, `music.` and `youtube-nocookie.com` with `/watch?v=`, `/shorts/`, `/embed/` or `/live/`, and `youtu.be/ID`. The video ID (letters, digits, `-` and `_`) is put in a built-in page.

The page's script posts to `https://www.youtube.com/youtubei/v1/player` with the clients `ANDROID_VR`, `IOS`, `TVHTML5_SIMPLY_EMBEDDED_PLAYER` and `WEB_EMBEDDED_PLAYER` in turn, and takes the first answer with `streamingData`. From `formats` (progressive streams that carry video and audio together) it picks the highest one up to 720p in MP4 or WebM, never AV1, sets it as the `src` of a `<video>`, and shows the title, channel, view count and the chosen quality. Playback goes through the media service and `play` with HTTP Range requests (section 13.17).

Limits: this could not be tested against youtube.com from the development machine; YouTube changes these clients often and may ask for sign-in or proof-of-origin tokens, and then the page shows the reason it was given. Adaptive-only videos (most above 720p), live streams and age-restricted videos do not play.

## 13.24 Limits raised for large web apps

| Limit | Value | Where |
|---|---|---|
| Script time | 15 s per call, plus 1 s per 40 KB of script source (at most 120 s more) | `arm`, `js_run_script` (`web/js.c`) |
| Engine arena | first of 768, 512, 384, 256, 160, 96, 64, 32 MiB that the heap can give while keeping 320 MiB free | `nwe_arena_init` (`web/krt.c`), `nwk_alloc_arena` (`apps/webglue.c`) |
| JavaScript heap | 70 percent of the arena | `js_init` |
| JavaScript stack | 3 MiB | `JS_SetMaxStackSize` |
| Engine stack | 6 MiB | `ESTACK_SZ` |

`Intl` is defined at the start of `web/prelude.js` when the runtime has none. It follows English formatting rules for every locale.

## 13.25 Staying responsive while a page works

The engine still runs on the desktop thread. Instead of a second thread, the long-running places call back into the kernel:

| Caller | How often |
|---|---|
| QuickJS interrupt handler (`interrupt_cb`, `web/js.c`) | on every check, rate-limited to one pump per 10 ms in `nwk_busy` |
| `style_tree` (`web/cssstyle.c`) and `layout_blocks` (`web/layout.c`) | `nwe_check_stop` every 128 elements or blocks |
| `net_poll` (`drivers/storage/ahci.c`) and `nd_poll` (`net/tcpip.c`) | `g_wait_pump` once per millisecond of waiting |

`busy_pump` (`gui/desktop.c`) polls USB mice, takes the PS/2 movement accumulated by the interrupt handler, moves the pointer with the same acceleration as the desktop and redraws only the pointer (`present_cursor`). Button presses are left for the main loop. It does nothing outside the desktop and is not re-entered.

If Esc is held while one engine call has been running for more than 1.5 seconds, `nwk_busy` returns 1. The interrupt handler then stops the running script and sets `g_nwe_user_stop`; in style or layout `nwe_check_stop` calls `nwe_fail` directly. `nwe_entry` unwinds, and the browser shows "This page stopped" (section 13.22).

Timers: `js_tick` runs due timers for at most 40 ms per tick. `setTimeout` and `setInterval` return 0 when 50,000 timers exist.

`data:` addresses are decoded in `browser_go` (percent-encoded or `;base64`) and loaded as HTML.

## 13.26 Layout on demand for scripts

`js_flush_n` (`web/js.c`) runs `eng_layout_if_dirty` before `getBoundingClientRect` and the geometry getters (`offset*`, `client*`, `scroll*`). If the element is in the document but has no box yet (the page is still loading and has never been laid out), it marks the styles dirty first. A counter allows 30 forced layouts per 50 ms; after that the old values are returned until the window passes. Re-entry from inside style or layout is ignored.

Measured in the host harness on a test page: before, every element reported 0x0 during load; after, `aspect-ratio`, flex `gap`, `clamp()`, `min()`, `display: contents`, `inset`, `:is()`, `:has()`, grid `repeat(3, 1fr)`, `calc()`, `margin-inline: auto`, `padding-block` and `fit-content` all reported the expected sizes and positions.

## 13.27 Bugs found against a modern landing page

A test page in the style of a current product landing page was rendered by the host harness (`tools/webhost.c`) and by Chromium at 1280x860, and the two images were compared.

| Bug | Fix |
|---|---|
| SVG `<text>` ignored | `svg_text` (`web/svg.c`): the element's text (whitespace collapsed, up to 1023 bytes, nested elements included) is drawn with `font_draw` at the transformed `x`/`y`. The size is `font-size` times the square root of the transform's determinant. The face comes from `css_face_for` (`web/cssstyle.c`, formerly the static `face_for`). |
| `color-mix()` and alpha | channels are mixed as premultiplied values and divided by the mixed alpha |
| comma inside `min()` in `repeat()` or `minmax()` | `top_comma` finds the separating comma at nesting depth 0 |
| `min(100%, 420px)` as a grid track | when a length has a percentage and an upper bound from `min()`, the track uses the bound in pixels |
| empty `getComputedStyle` during load | native 6 calls `js_flush_n` first |

After the fixes the header, hero, buttons, SVG logo with its text, separator and two-column section matched the Chromium rendering in position and size. Still different: `text-wrap: balance` is ignored, so lines break greedily.

## 13.28 New tab page and probe fixes

`resolve_area` (`web/cssstyle.c`) runs at the end of `compute`: when the element has a `grid-area` name and its parent has `grid-template-areas` (kept in `style.gta`), the rows and columns where the name appears give `grid-row-start/end` and `grid-column-start/end`.

`scrollHeight` returns the larger of the content height plus padding and the box height minus borders.

The new tab page (`g_page == 0` in `apps/browser.c`) is drawn by the browser itself, not by the web engine. "I'm Feeling Lucky" opens a random Wikipedia article. Up to 16 clickable areas are kept per page.

## 13.29 Faster loading and form controls

`js_flush_n` keeps the time spent in forced layouts per second of wall time. It lays out only while that total is under 150 ms, or never when nothing is dirty. Engine-internal callers that need fresh boxes (`scrollIntoView`, focus navigation) call `eng_layout_if_dirty` themselves.

The host harness now takes `WH_ACT`, a list of actions separated by semicolons: `c X Y` (move, press and release), `t TEXT` (type characters), `k CODE` (one key). A form test page (text field, textarea, button, a div with `role=button`, checkbox with label, select, a link styled as a button, a button with a `pointer-events: none` child, a search field and a form) gave the expected values, clicks, `input` and `change` events and submit.

## 13.30 Load time limits

| Limit | Value | Where |
|---|---|---|
| One script call, timer or event | 6 s | `arm` (`web/js.c`) |
| Extra for a large script | 1 s per 200 KB, at most 8 s | `js_run_script` |
| All page scripts during a load | 8 s, counted from `js_init`; later scripts are skipped | `g_load_js_ms` |
| Forced layouts for scripts | 60 ms per second | `js_flush_n` |
| Pointer pump during network waits | once per 10 ms tick | `busy_pump` (`gui/desktop.c`) |

`nwk_fetch` (`apps/webglue.c`) adds up the time and number of requests in `g_fetch_ms` and `g_fetch_n`. `browser_go` reports the time to fetch the page, the files fetched while the engine built it, and the engine's own time. The report also goes to the kernel log.
