# Part 13: The web engine, JavaScript and cookies

Since v9.08 the browser is a real layout engine with a full JavaScript runtime. This part describes it: how it is built and linked into the kernel, how the kernel talks to it, the HTML parser and DOM, the CSS cascade, layout (block, inline, floats, positioning, tables, flexbox and grid), painting and fonts, the QuickJS bindings and the JavaScript prelude, networking and the cookie jar, the browser window around it, the `qjs` command, and how to test and extend it.

Paths are relative to the repository root. The engine lives in `web/`; the kernel side is `apps/internet/webglue.c`, `apps/internet/browser.c` and `net/http/cookies.c`.

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
| `apps/internet/webglue.c` | 289 | Engine stack, HTTP client `nwk_fetch`, the `nwk_*` callbacks, console ring |
| `net/http/cookies.c` | 454 | Cookie jar and `localStorage`/`sessionStorage`, both saved to disk |
| `apps/internet/browser.c` | 698 | The Browser window: tabs, toolbar, about: pages, input routing, image decoding for the engine |
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

`web_call(op, r)` in `apps/internet/webglue.c` runs every op on a separate 2 MiB stack through the assembly trampoline `nwe_call_stack` (`arch/x86_64/user/stack_call.c`). QuickJS and the recursive layout code need more stack than the kernel's boot stack can spare. A guard (`g_in_engine`) refuses re-entry: if a callback such as a fetch pumps the GUI and something tries to call the engine again, `web_busy()` is true and the browser draws "Loading..." instead.

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

## 13.9 Networking and the HTTP client (`apps/internet/webglue.c`)

`nwk_fetch(url, method, ctype, body, blen, referer, resp)` is the only HTTP client the engine uses:

- HTTP/1.1 with `Connection: close`: plain HTTP over the kernel TCP stream (`tcps_*`), HTTPS over the kernel TLS 1.3 client (`tls_get`) with certificate checks;
- up to 7 redirects (301, 302, 303, 307, 308; a 303, or a POST answered with 301/302, continues as GET);
- chunked transfer and `gzip` content encoding (`Accept-Encoding: gzip`; a `deflate` body is also decoded);
- `Cookie` headers from the jar and `Set-Cookie` back into it, on every request including subresources and `fetch()`;
- a Chrome-like `User-Agent` (`WEB_UA`, ends in `Noovex/9.08`) so sites send their normal pages;
- `Referer`, `Accept` and `Accept-Language: en-US` headers.

Typed addresses without a scheme try HTTPS first and fall back to HTTP.

## 13.10 Cookies and web storage (`net/http/cookies.c`)

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

The old `ck_*` functions in `net/http/http.c` (used by the `http` command and the account code) are now wrappers around the same jar.

## 13.11 The Browser window (`apps/internet/browser.c`)

The window keeps the toolbar from earlier versions (back, reload, bookmark star, address bar, Go, Save, Home, Find, and a new Cookies button) and up to 6 tabs. Switching tabs reloads the page of that tab.

| Page | Contents |
|---|---|
| `about:blank` | empty page |
| `about:js` | a JavaScript demo page |
| `about:cookies` | every stored cookie by site with its flags; buttons to clear everything (`?clear`) or remove one site (`?del=`) |
| `about:console` | the last console messages and script errors |
| `about:history`, `about:bookmarks` | history and bookmarks |

Downloads (`Content-Disposition: attachment` or a file extension like `.zip`) go to Downloads, and a direct link to an image opens the image viewer. Network errors show an error page with the reason (DNS, connect, timeout, TLS).

Input. Mouse moves send `MOVE` (hover styles, link cursor, status-bar URL); a click sends `DOWN` and `UP`; the wheel goes to `browser_wheel`; keys are mapped by `map_key` (the kernel's arrow, Home, End, Page and Delete codes become `NWE_K_*`). `browser_tick` sends `TICK` from the main loop (`graphics/ui/input.c`) and asks for a redraw when the page changed.

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

- No WebGL, `OffscreenCanvas`, Web Workers or WebAssembly. WebSockets work (section 13.33). Only one `<iframe>` per page shows its content (section 13.35). Canvas 2D (section 13.31) has no shadows, no real patterns, no `toDataURL` image data and rectangular clipping only. `<video>` has no subtitles, no encrypted media (EME) and plays at speed 1 only (section 13.17).
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
- A new kernel service for the engine: add an `nwk_` function to `nwe_api.h`, implement it in `apps/internet/webglue.c` (and a stub in `tools/webhost.c`), and remember that it runs on the engine stack.
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

The engine runs on its own 2 MiB stack (`g_estack`, `apps/internet/webglue.c`). When a CPU exception happens in kernel mode, `isr_body` (`arch/x86_64/interrupts/interrupts.c`) first calls `web_fault_redirect`. If the browser is inside an engine call and the faulting stack pointer is in the engine stack (or just below it, for an overflow), the interrupt frame is changed so that `iretq` lands in `web_fault_land` on a safe part of that stack. `web_fault_land` calls `nwe_fail`, which unwinds to the `setjmp` in `nwe_entry`; the engine marks the page dead and returns -1.

`web_call` then sets `g_web_failed`. `browser_tick` sees it and shows "This page crashed" (`browser_crashed`); the next load resets the engine. NMI, double fault and machine check are never redirected, and after 64 redirected faults the normal error screen is shown again, so a broken engine cannot loop forever.

`about:crash` writes to a non-canonical address inside the engine to test this path.

## 13.23 YouTube player page

Since v9.15 `yt_try` is no longer called, so YouTube addresses load the real site. The description below is kept for the code that is still in `apps/internet/browser.c`.

YouTube's desktop site needs far more JavaScript and layout than the engine handles, so `yt_try` (`apps/internet/browser.c`) recognises watch addresses before any network request: `youtube.com`, `www.`, `m.`, `music.` and `youtube-nocookie.com` with `/watch?v=`, `/shorts/`, `/embed/` or `/live/`, and `youtu.be/ID`. The video ID (letters, digits, `-` and `_`) is put in a built-in page.

The page's script posts to `https://www.youtube.com/youtubei/v1/player` with the clients `ANDROID_VR`, `IOS`, `TVHTML5_SIMPLY_EMBEDDED_PLAYER` and `WEB_EMBEDDED_PLAYER` in turn, and takes the first answer with `streamingData`. From `formats` (progressive streams that carry video and audio together) it picks the highest one up to 720p in MP4 or WebM, never AV1, sets it as the `src` of a `<video>`, and shows the title, channel, view count and the chosen quality. Playback goes through the media service and `play` with HTTP Range requests (section 13.17).

Limits: this could not be tested against youtube.com from the development machine; YouTube changes these clients often and may ask for sign-in or proof-of-origin tokens, and then the page shows the reason it was given. Adaptive-only videos (most above 720p), live streams and age-restricted videos do not play.

## 13.24 Limits raised for large web apps

| Limit | Value | Where |
|---|---|---|
| Script time | 15 s per call, plus 1 s per 40 KB of script source (at most 120 s more) | `arm`, `js_run_script` (`web/js.c`) |
| Engine arena | first of 768, 512, 384, 256, 160, 96, 64, 32 MiB that the heap can give while keeping 320 MiB free | `nwe_arena_init` (`web/krt.c`), `nwk_alloc_arena` (`apps/internet/webglue.c`) |
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
| `net_poll` (`drivers/storage/ahci.c`) and `nd_poll` (`net/stack/tcpip.c`) | `g_wait_pump` once per millisecond of waiting |

`busy_pump` (`graphics/ui/desktop.c`) polls USB mice, takes the PS/2 movement accumulated by the interrupt handler, moves the pointer with the same acceleration as the desktop and redraws only the pointer (`present_cursor`). Button presses are left for the main loop. It does nothing outside the desktop and is not re-entered.

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

The new tab page (`g_page == 0` in `apps/internet/browser.c`) is drawn by the browser itself, not by the web engine. "I'm Feeling Lucky" opens a random Wikipedia article. Up to 16 clickable areas are kept per page.

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
| Pointer pump during network waits | once per 10 ms tick | `busy_pump` (`graphics/ui/desktop.c`) |

`nwk_fetch` (`apps/internet/webglue.c`) adds up the time and number of requests in `g_fetch_ms` and `g_fetch_n`. `browser_go` reports the time to fetch the page, the files fetched while the engine built it, and the engine's own time. The report also goes to the kernel log.

## 13.31 Canvas 2D (v10.10)

`<canvas>` has a real 2D context. The pixels live in an `nimg` hung on the element (`n->img`, non-premultiplied ARGB, created by `cv_get` in `web/canvas.c` at the size of the `width`/`height` attributes, 300x150 by default, at most 8192 on a side and 16 M pixels). `paint_replaced` draws it like an image, scaled to the CSS box.

| Layer | Contents |
|---|---|
| `web/prelude.js` | `CanvasRenderingContext2D` with its state (`fillStyle`, `strokeStyle`, `lineWidth`, `lineCap`, `lineJoin`, `miterLimit`, `globalAlpha`, `font`, `textAlign`, `textBaseline`, the line dash, the transform and a clip rectangle) and `save`/`restore`; paths (`moveTo`, `lineTo`, `closePath`, `rect`, `roundRect`, `arc`, `arcTo`, `ellipse`, `quadraticCurveTo`, `bezierCurveTo`) flattened to device-space polygons; strokes turned into polygons (segment quads, miter, bevel and round joins, butt, square and round caps, dashes); `fillRect`, `strokeRect`, `clearRect`, `fill`, `stroke`, `clip` (bounding box), `isPointInPath`; `fillText`, `strokeText`, `measureText`; `drawImage` (3, 5 and 9 arguments, from `<img>` or another canvas); `getImageData`, `putImageData`, `createImageData`; `createLinearGradient`, `createRadialGradient`; `translate`, `scale`, `rotate`, `transform`, `setTransform`, `getTransform`, `resetTransform`, `reset`; `Path2D` (including SVG path strings with M, L, H, V, C, Q, Z) and `ImageData`. Setting `width` or `height` clears the canvas and resets the state. |
| `web/js.c` | The natives `cvInit`, `cvFill`, `cvText`, `cvImage`, `cvGet`, `cvPut`, `cvColor` (CSS colour parsing through `css_parse_color`) and `textWidth`. They mark the page for repaint. |
| `web/canvas.c` | `cv_fill`: polygons with anti-aliasing by signed-area accumulation (the method `font.c` uses for glyphs) over the bounding box only, solid colours or linear and radial gradients, `source-over` blending, or erasing for `clearRect` and `destination-out`. `cv_text` draws the glyphs with `font_draw` into a scratch buffer and uses it as coverage. `cv_image` copies with scaling and alpha. `cv_getdata`/`cv_putdata` convert to and from RGBA bytes. |

Overlapping polygons of one fill are combined with the non-zero rule as a union (coverage is clamped at 1), so `evenodd` holes are not supported.

Tested with `tools/webhost.c` (rectangles, a stroked rectangle, a filled circle, a triangle with round joins, a linear and a radial gradient, a rotated rectangle, text, a dashed Bezier curve, `clearRect`, `getImageData`, `drawImage` from the same canvas, `measureText`, `isPointInPath`) and in QEMU, including an animation driven by `requestAnimationFrame`.

## 13.32 Grid layout in Rust (v10.10)

`layout_grid` (`web/layout2.c`) hands CSS Grid containers to Taffy, a Rust layout library (`rust/nvxlayout`, section 4.9 of Part 1). `taffy_grid` converts the container (column and row tracks after `repeat()` is expanded, the implicit row size, gaps, `justify-content`, `align-content`, `justify-items`, `align-items`, `grid-auto-flow` including `dense`) and each in-flow item (line placement and spans, explicit width and height, margins including `auto`, `justify-self`, `align-self`, replaced or not) into plain C structures and calls `nvx_taffy_grid`. Taffy runs the full track sizing algorithm and asks back through `nvx_grid_measure` for item sizes: min-content and max-content widths come from `intrinsic`, heights from laying the item out at the given width with `lay`. The results (position and size of every item) are applied with `lay` and `shift_box`. Absolutely positioned children are handled by the engine as before.

The container's `min-height` and `max-height` (in px) are passed too, so `fr` rows grow to fill a grid with only a minimum height (a holy-grail page with `grid-template-rows: auto 1fr auto` and `min-height: 260px`; the old C code left the middle row at its content height).

If Taffy returns an error or a size that is not a number, the old C grid code runs instead, and `g_taffy_grid = 0` switches Taffy off. A Rust panic ends the page with "layout engine (Rust) stopped" (through `nwe_fail`), never the kernel.

Check: a 200px / 1fr / 2fr grid in a 620 px container with 10 px gaps gives the 1fr column 133.3 px; `repeat(auto-fill, minmax(120px, 1fr))`, spans, `grid-column: 1 / -1`, `justify-items: center` and `justify-self: end` render as in other browsers.

## 13.33 WebSockets (v10.11)

`WebSocket` is real (RFC 6455), for `ws://` and `wss://`. The JavaScript class is in `web/prelude.js`; the protocol runs in the kernel in `apps/internet/websock.c`, reached through the natives `wsOpen`, `wsSend`, `wsRecv`, `wsInfo` and `wsClose` (`web/js.c`, magic 50-54) and the `nwk_ws_*` callbacks (`nwe_api.h`).

| Step | What happens |
|---|---|
| Open | `nwk_ws_open` resolves the host, opens a Linux-style TCP socket (`lxs_*`), and for `wss://` runs the TLS 1.3 handshake through the new session API (below). It sends the HTTP Upgrade request (with the page's cookies and `Origin`), checks the `101` reply and the `Sec-WebSocket-Accept` value (SHA-1 of the key plus the RFC GUID, Base64), and reads `Sec-WebSocket-Protocol` and any `Set-Cookie`. |
| Frames | Client frames are masked (random 4-byte key, as the standard requires); server frames may be fragmented. Text (opcode 1), binary (2), ping (9, answered with pong), pong (10, ignored) and close (8) are handled. Messages up to 8 MiB; the receive buffer grows as needed. |
| Receive | `WebSocket` polls `wsRecv` on a 30 ms timer in the prelude and fires `message` events (text as a string, binary as an `ArrayBuffer` or `Blob` by `binaryType`). Closing fires `close` with the code, reason and `wasClean`; a dropped connection gives code 1006 and an `error` event first. |
| Send | `send` masks and frames the data; `close(code, reason)` sends a close frame and waits briefly for the peer's. `ws`/`wss` URL parsing, the ready-state machine, `bufferedAmount` (always 0) and the close-code checks follow the WHATWG API. |
| Reset | A page load closes every socket (`nwk_ws_close_all` in `reset_engine`). |

TLS session API. `tls_get` (section 9.9.2) handshook, sent one request and read the whole reply. WebSockets need to keep sending and receiving after the handshake, so `lib/crypto/nvxtls.c` now also exposes `tls_connect_sess` (handshake into a `tls_sess` holding the cipher, keys, IVs and record sequence numbers), `tls_sess_write`, `tls_sess_read` and `tls_sess_wipe`. `tls_get` is now a thin wrapper over them, so HTTPS fetching is unchanged.

Tested with `tools/webhost.c` plus a host WebSocket shim over real TCP against a Python `ws://` echo server: connect, text echo, a second text message, a binary message echoed reversed, server ping answered, and a clean close (code 1000); and in QEMU against a local server reached as `10.0.2.2:8090`.

Limits: no `permessage-deflate` compression, no subprotocol negotiation beyond echoing what the server picks, and `wss://` uses the browser's TLS 1.3 client (section 9.9), so a server that needs a cipher or group it does not offer will not connect.

## 13.34 Popups and postMessage for sign-in (v10.11)

The browser still has one page per window, so it cannot open a real popup. `window.open(url, name, features)` with a features string (what "Sign in with Google" and similar use) now returns a detached window object that reports `closed === true` with working `postMessage`, `location` and `close` members, instead of navigating the current page away. Popup-based sign-in libraries read that as "popup blocked" and fall back to a full-page redirect, which the browser can follow; cookies set during the redirects are kept (section 13.10), so the redirect form of OAuth can complete. `window.open(url, '_self')` still navigates the current page. `postMessage` now structure-clones its data and carries the target origin. This was tested in `webhost` (a blocked-popup object with a working `postMessage`, and a delivered message); the full third-party sign-in flow needs a live identity provider and was not tested here.

## 13.35 Iframes (v10.12)

The engine keeps one document in global state, so a page and its iframe cannot share one engine. Instead the kernel links a second, complete copy of the engine for the iframe.

Build. `build.sh` makes `web2_all.o` from the same `web_pre.o` with `objcopy --redefine-sym`: `nwe_entry` becomes `nwe2_entry` and `nwe_fail` becomes `nwe2_fail`, and the frame copy's kernel callbacks are pointed at frame versions (`nwk_alloc_arena2` caps its arena at 256 MiB, `nwk_frame_post` becomes `nwk_frame_post_up`, `nwk_frame_recv` becomes `nwk_frame_recv_down`, `nwk_frame_is` becomes `nwk_frame_is2`, and `nwk_frame_paint`, `nwk_frame_reset` and `nwk_ws_close_all` become no-ops so the frame cannot nest frames or close the page's WebSockets). Both copies keep every other symbol local, so they share nothing but the kernel. The kernel image grows by about 3.6 MB.

| Step | Where | What happens |
|---|---|---|
| Ask | `paint_replaced` (`web/paint.c`) | When the page paints an `<iframe>` with a `src` (resolved against the page) or `srcdoc`, it calls `nwk_frame_paint` with the box and clip. The first visible iframe of a page claims the frame engine; other iframes keep the "Embedded page" placeholder. |
| Hidden | `frameScan` (`web/prelude.js`), native `frameOpen` (magic 58), `nwk_frame_open` | Every 40 ms the page looks for the first `iframe[src]`, including ones that are hidden (`display: none`, 0x0) and so never painted, and asks the kernel to load it at 300x150. Invisible challenge iframes therefore load and run their scripts. |
| Load | `frame_tick` (`apps/internet/webglue.c`, from `browser_tick`) | Outside any engine call, the kernel fetches the URL with `nwk_fetch` (cookies included, the page's address as `Referer`) or takes the `srcdoc` text, and sends `NWE_OP_LOAD` to the frame engine at the iframe's size. A navigation inside the frame (a link, `location`) loads the new URL into the frame. |
| Paint | `nwk_frame_paint` | Each time the page paints, the frame engine paints into a buffer the size of the iframe, which is copied into the screen inside the page's clip. A size change sends `NWE_OP_LAYOUT` first. |
| Run | `frame_tick` | The frame engine gets `NWE_OP_TICK` with the page, so its timers, promises and animations run. |
| Input | `web_mouse` (`apps/internet/browser.c`) | A click inside the visible iframe goes to the frame engine (move, down, up) at frame coordinates instead of the page. |
| Messages | `web/prelude.js`, natives `framePost`, `frameRecv`, `isFrame` (`web/js.c`, magic 55-57) | `iframe.contentWindow` and, inside the frame, `window.parent`/`window.top` are window proxies with `postMessage`. Messages are serialized as JSON with the sender's origin and target origin, queued in the kernel (64 per direction, 256 KB each) and delivered by a 40 ms poll as `message` events with the right `origin` and `source`; a target origin that does not match the receiver drops the message. |
| Reset | `reset_engine` | A new page load forgets the frame and clears both queues. |
| Crash | `web_fault_land` | A CPU exception while the frame engine runs stops only the frame (`nwe2_fail`); the page stays. |

Limits: one live iframe per page; no keyboard focus or wheel scrolling inside it; `contentDocument` is `null` (the documents live in different engines, as for a cross-origin frame); data sent with `postMessage` must survive JSON (no `Blob`, `ArrayBuffer` or `MessagePort` transfer); `sandbox` and `allow` attributes are not applied.

Why: challenge pages such as Cloudflare's run their check inside an iframe and report the result to the page with `postMessage`. This gives that check a place to run; whether a given service accepts the browser is decided by that service and was not tested here (the build machine cannot reach it).

## 13.36 Reloads, tabs and the toolbar (v10.13)

Bug: since v10.09 the second page load of a session (Reload, a new address, a tab switch, which reloads the tab's page) showed a white page. Cause: `web_load_html` (`apps/internet/browser.c`) filled the shared request block `g_req` and then called `compose_present()` to show "Loading" before sending `NWE_OP_LOAD`. Once a page was showing, that redraw ran `web_paint`, which cleared `g_req` and used it for `NWE_OP_LAYOUT`/`NWE_OP_PAINT`, so the engine received a load with no URL and length 0 and built an empty document. The first load of a session was fine because `web_paint` returns early while there is no page. With several tabs every switch hit this, so most tabs showed nothing.

| Fix | Where |
|---|---|
| `compose_present()` runs before the request is filled, not after. | `web_load_html` |
| `web_paint` uses its own static request (`preq`) and the hover probe in `browser_draw` its own (`mreq`), so drawing can never clobber a request in flight. | `apps/internet/browser.c` |
| The streaming TCP reader accepts a FIN only when it is in sequence (`seq == ts_rcv`); an early FIN after a missed segment is answered with a duplicate ACK so the server resends the data, instead of ending the response early. | `tcps_rd` (`net/http/http.c`) |

Toolbar (drawing only, all click areas keep their variables): a tab strip in a darker band with the active tab joined to a light toolbar; each tab has a coloured circle with the site's first letter, a title cut with ".." to fit, and a larger close cross; round back, reload (circular arrow) and star buttons; a rounded address field that, when not being edited, shows a lock (green for https, grey for http), the host in dark text and the rest of the address in grey, cut with ".." at the end instead of the start; "Search or enter address" when empty; pill buttons; a thin accent bar under the toolbar while the engine is busy.

Tested in QEMU: one page reloaded twice, then three tabs with different pages switched back and forth five times; every switch and reload showed the right page.

## 13.37 Web Crypto and structuredClone (v10.14)

`crypto.subtle` was an empty object, so pages that hash data (for example the PKCE step of an OAuth sign-in, which sends the SHA-256 of a random verifier) failed. It is now implemented in JavaScript in `web/prelude.js`:

| Method | Supported |
|---|---|
| `digest` | SHA-256, SHA-1 |
| `importKey` (`raw`) | HMAC (SHA-256 or SHA-1), PBKDF2 |
| `sign`, `verify` | HMAC; `verify` compares in constant time |
| `deriveBits` | PBKDF2 with HMAC-SHA-256 or HMAC-SHA-1 |
| `generateKey` | HMAC (random key from `crypto.getRandomValues`) |
| `exportKey` (`raw`) | extractable keys |
| `encrypt`, `decrypt`, other algorithms | reject with `NotSupportedError` |

Results are Promises of `ArrayBuffer`, inputs are any `BufferSource`, and unknown algorithms reject with `NotSupportedError` as in browsers. `CryptoKey` keeps the key bytes in a non-enumerable property.

`structuredClone` used a JSON round trip, which turned `Date` into strings and dropped `Map`, `Set`, typed arrays and cycles. It now copies `Date`, `RegExp`, `ArrayBuffer`, typed arrays and `DataView`, `Map`, `Set`, `Error`, boxed primitives, arrays (holes kept) and plain objects, keeps shared and cyclic references, passes `Blob`s through, and throws `DataCloneError` for functions, symbols and DOM nodes.

Tested: SHA-256 ("abc" and 1000 bytes), SHA-1, HMAC-SHA-256 and PBKDF2-HMAC-SHA1 against Python's `hashlib`/`hmac` in QuickJS on the host, and SHA-256, HMAC and `structuredClone` on a page in the browser in QEMU.

JavaScript speed: building QuickJS with -O3 or -march=x86-64-v2 instead of -O2 gave no measurable gain on a host benchmark (recursion, object sort, string building with a regex, Map), and computed-goto dispatch is already on, so there was no quick compiler win. Real gains need an inline cache or a JIT (estimate 15-30 sessions for a JIT).

## 13.38 Framework compatibility, incremental restyle and stability (v10.15)

Method. Gmail, Google Docs, YouTube, GitHub and claude.ai cannot be reached from the build machine, so their public building blocks were installed from the machine's own package cache and run in the engine on the host (`tools/webhost.c`) next to Chromium (Playwright) as the reference: React 19 with Markdown (marked), DOMPurify, highlight.js, KaTeX, Jotai, Zod, TanStack Virtual and Headless UI (a chat app in the style of claude.ai); a GitHub-style repository page with Catalyst-style custom elements, `relative-time`, `details-menu`, `clipboard-copy`, a Turbo-style fetch-and-replace navigation and Primer-like CSS; a YouTube-style grid of 60 shadow-DOM components rendered with a Lit-style template engine (template element, `importNode`, `TreeWalker`), constructable stylesheets, `::part`, `::slotted` and lazy images through `IntersectionObserver`; a Docs-style `contenteditable` editor; D3 and Mermaid charts; and a 1,500-row inbox page with 3,000 CSS rules for speed. All of them now render close to Chromium and react to clicks and typing; differences left are font metrics and emoji glyphs (shown as "?"); SVG markers and emoji were added later in this release (section 13.39).

| Area | Before | Now | Where |
|---|---|---|---|
| Custom elements | `connectedCallback` only ran at `define` time; constructors never ran on upgrade; elements created by `innerHTML` or the parser after `define` were never upgraded; no `disconnectedCallback` or `attributeChangedCallback` | Full lifecycle: the parser and `elem_new` record every hyphenated element in `g_ce_pend` (native `cePending`, magic 59); upgrades run the real constructor (the `HTMLElement` constructor returns the element being upgraded), `observedAttributes` callbacks fire for existing attributes, then `connectedCallback`; insertions, removals and attribute changes go through the mutation hook (new kind 5 = attribute changed, fired after the change with the old value); `innerHTML`/`outerHTML`/`insertAdjacentHTML` and shadow-root `innerHTML` flush upgrades and connect/disconnect; template contents stay inert; name and constructor checks as in browsers; `customElements.upgrade` | `web/dom.c`, `web/js.c`, `web/prelude.js` |
| Mutation events during load | Off while any inline script ran during parsing (`g_dom_parsing`), so `MutationObserver` and custom elements missed everything a page did at load | `js_run_script` clears the parsing flag while a script runs; the hook may nest (depth 8) | `web/js.c` |
| `isConnected` | False inside shadow trees | Walks through shadow hosts | `web/js.c` |
| Adopted stylesheets | A hidden `<style>` child that frameworks removed on their first render | Stored on the shadow root node (`value`, native `setAdopted`, magic 60) and parsed by `collect` with the host scope; `replaceSync`/`insertRule` re-apply to every root that adopted the sheet | `web/cssparse.c`, `web/js.c`, `web/prelude.js` |
| SVG elements | Only tags in the tag table were SVG; `text`, `tspan`, `foreignObject`, `defs`, gradients and markers made with `createElementNS` or parsed inside `<svg>` were plain HTML elements; `getBBox` and friends were missing | `NF_SVG` node flag from `createElementNS` and the parser (children of `foreignObject` stay HTML); `getBBox` (shapes, paths with curves, text with anchor and baseline, groups with transforms), `getComputedTextLength`, `getSubStringLength`, `getTotalLength`, `getPointAtLength`, `getCTM`, `getScreenCTM`, `createSVGPoint`/`Matrix`/`Rect`/`Transform`, `ownerSVGElement`, `transform.baseVal`; `getBoundingClientRect` of SVG elements and of HTML inside `foreignObject` is estimated from geometry and text | `web/nwe.h`, `web/dom.c`, `web/js.c`, `web/prelude.js` |
| SVG painting | Text attributes were read only from the `text` element; `<tspan>` positions and `em` offsets ignored; `<style>` inside `<svg>` ignored; `foreignObject` not drawn | `font-size`, `text-anchor`, weight, style, family and baseline inherit through groups; `x`/`y`/`dx`/`dy` per `tspan` with `em` units and anchored text chunks; `<style>` blocks inside `<svg>` are collected; `foreignObject` draws its text centred in its box | `web/svg.c`, `web/cssparse.c` |
| SVG sizing | A `<svg>` with a `viewBox` and no `height` was 150 px high (UA rule `svg:not([height])`); an SVG image without `viewBox` was rasterised at the wrong scale | The 300x150 UA defaults apply only without a `viewBox`; with a `viewBox` and no size the SVG fills the available width and takes its height from the ratio; image rasterisation adds the implied `viewBox` | `web/cssstyle.c`, `web/layout.c`, `web/engine.c` |
| Tables | `rows`, `cells` and friends were missing (`rows` reflected the textarea attribute) | `rows`, `tBodies`, `tHead`, `tFoot`, `caption`, `create*`/`delete*`, `insertRow`/`deleteRow` on tables and sections, `cells`, `rowIndex`, `sectionRowIndex`, `insertCell`/`deleteCell`, `cellIndex`, `colSpan`, `rowSpan` | `web/prelude.js` |
| Rich text editing | `execCommand` knew only plain text commands; no `queryCommandState` | `bold`, `italic`, `underline`, `strikeThrough`, `subscript`, `superscript` (toggle, split and merge), `formatBlock`/`heading`, lists, `createLink`/`unlink`, `removeFormat`, `foreColor`, `hiliteColor`/`backColor`, `fontName`, `fontSize`, `justify*`, `indent`/`outdent`, `insertHorizontalRule`, `insertImage`, `insertLineBreak`, `undo`/`redo` (100 steps per editor); `queryCommandState`, `queryCommandValue`, `queryCommandEnabled`, `queryCommandIndeterm` | `web/prelude.js` |
| `innerText` | `textContent` with collapsed spaces | Rendered-text rules: line breaks around blocks, two around paragraphs, `<br>`, tabs between cells, newlines between rows, hidden and `display:none` content skipped, `pre` kept | `web/js.c` (`itx_walk`) |
| Closed `<details>` | Author CSS such as `display:flex` showed its content | Everything but the summary is forced to `display:none` while `open` is absent, as in browsers | `web/cssstyle.c` |

Incremental restyle. A class, attribute, hover, focus or text change used to restyle the whole document and lay it out again, and an id or class change even rebuilt the rule index (`css_bump`). Now `eng_style_node(n)` records up to 48 subtree roots (`G.dirty_style = 2`); `style_partial` (`web/cssstyle.c`) recomputes only those subtrees, with the ancestor filter primed from the root's ancestors, and writes each new style over the old style object in place, so boxes and text fragments keep valid pointers. `lay_eq` compares only the properties that affect layout; if none changed, layout is skipped and only a repaint happens (colours, backgrounds, borders' colours, shadows, outlines, radius, opacity changes that keep the same stacking, transforms, cursor). The root is widened to the parent when the style sheet has `+`/`~` combinators (`g_css_sib`) and insertions restyle the parent when it has positional pseudo-classes (`g_css_pos`); `:has()` anywhere (`g_css_has`), a changed style sheet, a root inside a `display:none` subtree, or garbage beyond three times the last full restyle fall back to a full restyle. Hover, `:active` and focus changes restyle only the elements whose state changed (`eng_chain_dirty`, from the lowest common ancestor down). Typing in an input or textarea restyles only that control and lays out only for a textarea. The UA rule `fieldset+p` (a typo that forced sibling handling everywhere) was removed. Animations outside a partial root keep running (`anim_after_style_part`).

CSS rule lookup. Rules whose rightmost compound has only an attribute selector (`[data-state=open]`, `[aria-expanded]`) were checked for every element; they are now bucketed by attribute name (`battr`).

Measured on the host (`webhost`, 1,500-row inbox with 3,000 rules): page load 187 ms to 118 ms; 20 class toggles each followed by a layout query 124 ms to 15 ms. In QEMU under TCG emulation (final build) the React chat app loads in 3.0 s and in 0.64 s on reload (code cache, section 13.39), the GitHub-style page in 0.8 s (2.2 s with the first v10.15 build, before the detached-tree and cache work) and the YouTube-style grid in 1.3 s.

Stability. A sweep over 260 real HTML documents found on the build machine (library documentation, man pages, NetSurf resources), 18 test pages and 557 randomly mutated pages under ASan and UBSan found and fixed:

| Bug | Effect | Fix |
|---|---|---|
| `add_text_pieces` (`web/layout.c`): a carriage return in `white-space: pre` text matched no branch and the word scanner stopped without advancing | Any page with a `<pre>` block and Windows line endings froze the browser (NetSurf licence page, the ncurses HOWTO: more than 2 minutes, now 90 ms) | Skip the character when the word is empty |
| `collect` passed a missing `href` of a `<link>` to `url_resolve` | Crash (NULL dereference) | `url_resolve` treats NULL as empty |
| Huge font sizes (`font-size: 1e30px`) | Integer overflow in font metrics and glyph code | `font-size` clamped to 10,000 px in the cascade and 100,000 px in `font_metrics` |

The second fuzz batch (260 pages) ran clean. `webhost` got `WH_PRE` (JavaScript evaluated before the timer loop) and a `w X Y D` wheel action; its `readfile` rejects unreadable paths.

Not done, with estimates: a JIT or inline caches for QuickJS (15-30 sessions; compiler flags gave nothing measurable, see 13.37); incremental layout (layout still runs for the whole page when a layout property changes, 3-5 sessions); SVG filters and gradients by `url()`; symbols outside the fonts and the emoji table still show as "?". The real Gmail, Docs, YouTube and claude.ai still need far more JavaScript speed and APIs (Service Workers, WebAssembly for some) and were not tested. Keyboard modifiers, IndexedDB, Workers and SVG markers were added later in this release (section 13.39).

## 13.39 Code cache, HTTP cache, IndexedDB, Workers and keys (v10.15)

Profile of the React chat app load: 78% of the time was QuickJS parsing and compiling the 900 KB bundle and 14% running it, so the speed work went where real browsers also put it.

| Feature | How it works | Where |
|---|---|---|
| Bytecode cache | Classic scripts of 8 KB or more are compiled with `JS_EVAL_FLAG_COMPILE_ONLY`, written with `JS_WriteObject(JS_WRITE_OBJ_BYTECODE)` into a kernel-side cache (`nwk_code_put`, 96 entries, 40 MiB, least recently used dropped first) keyed by a 64-bit FNV hash of the source text, its name and length, then run with `JS_EvalFunction`. On the next load of the same script `nwk_code_get` returns the bytecode and `JS_ReadObject` replaces parsing. The cache lives outside the per-page arena, so it survives reloads, tab switches and navigation; only the engine writes it, which is what makes `JS_READ_OBJ_BYTECODE` safe. Modules are not cached. Host numbers: React chat app 157 ms first load, 60-79 ms after; D3 + Mermaid page 745 ms, then 146-212 ms. The first load pays about 15-20 ms for writing. | `web/js.c`, `apps/internet/webglue.c`, `tools/webhost.c`, `include/nvx/apps/nwe_api.h` |
| HTTP memory cache | `nwk_fetch` keeps GET responses with status 200 (256 entries, 48 MiB, LRU) unless the response says `no-store` or sets a cookie. A response is reused while it is younger than its `Cache-Control: max-age` (capped at one day); scripts, style sheets, fonts and images without a lifetime are reused for 10 minutes. `g_http_mode` 1 (tab switch, Back) lets the first fetch, the page itself, use any cached copy younger than 10 minutes; mode 2 (Reload) bypasses the cache for the page and all its resources. The cookies page clears the cache too. | `apps/internet/webglue.c`, `apps/internet/browser.c` |
| IndexedDB | In memory, per origin, for the session: `indexedDB.open` with versioned `upgradeneeded`, `blocked`/`versionchange`, `deleteDatabase`, `databases`, `cmp`; object stores with in-line or out-of-line keys and key generators, `add`/`put`/`get`/`getKey`/`getAll`/`getAllKeys`/`count`/`delete`/`clear`; indexes with `unique` and `multiEntry`; cursors (`openCursor`, `openKeyCursor`, `continue`, `advance`, `update`, `delete`, all four directions); `IDBKeyRange`; transactions that complete after their last request, abort on an unhandled request error and roll back their stores; spec key ordering (number < date < string < binary < array); values copied with `structuredClone`. | `web/prelude.js` |
| Web Workers | Same-thread workers: the script is loaded from a URL, `blob:` or `data:` URL and run inside `with (scope)` so `onmessage = ...`, `self`, `postMessage`, `close`, `importScripts` (global eval), `addEventListener` and timers belong to the worker; messages are copied with `structuredClone` and delivered asynchronously both ways; `terminate`; load failures fire `error`. Module workers that use `import` report an error. Heavy work in a worker still blocks the page, because the engine has one JavaScript thread. | `web/prelude.js` |
| Keyboard | The Browser passes Ctrl, Shift, Alt and the Windows key as bits 24-27 of the key; Ctrl+letter arrives as the letter instead of a control code (which used to be read as Home, End and so on). Pages get `ctrlKey`, `shiftKey`, `altKey`, `metaKey`, `getModifierState`, physical `code` values (`KeyK`, `Digit1`, `Space`, `Slash`, ...), `keyCode`/`which` for shifted digits, and a `keyup` after every key. Ctrl and the Windows key suppress `keypress` and text insertion, so shortcuts reach the page. | `apps/internet/browser.c`, `web/engine.c`, `web/prelude.js` |
| Emoji | `tools/mkemoji.py` turns the CBDT bitmaps of Noto Color Emoji into `assets/emoji32.bin` (1,413 single-code-point emoji from the symbol and emoji ranges, 32x32, 128-colour palette PNGs, 1.44 MB; header `NVEM`, count, size, then sorted code point/offset/length entries). The kernel links it as a blob and serves `nwk_emoji_has` (binary search) and `nwk_emoji` (decode through `nwk_image`). When a code point has no glyph in the page fonts but is in the table, `font_adv` gives it 1.2 em and `font_draw` blits it scaled with bilinear filtering and the text colour's alpha; up to 96 decoded emoji are cached per page. Joined sequences (ZWJ, skin tones, flags) show as their parts. Tested in QEMU: the folder and file emoji of the GitHub-style page. | `tools/mkemoji.py`, `assets/emoji32.bin`, `apps/internet/webglue.c`, `web/font.c`, `build.sh` |
| SVG markers | `marker-start`/`marker-end` (attribute or style) on paths, lines and polylines draw the referenced `<marker>` at the end point with `orient` (`auto`, `auto-start-reverse`, angle), `refX`/`refY`, `markerWidth`/`markerHeight`, `viewBox` and `markerUnits`. | `web/svg.c` |

Tested against Chromium: IndexedDB (upgrade, stores, indexes, multiEntry, cursors in reverse, key ranges, rollback on abort, constraint error, array keys, `cmp`, version error, `databases`) and Workers (URL with `importScripts`, `blob:`, `data:`, failing URL) give the same results; key events for Ctrl+K, Shift+A, Alt+1, `!` and Enter carry the same `key`, `code`, `keyCode` and modifier values.


## 13.40 WebAssembly, Service Workers and the Cache API (v10.16)

WebAssembly runs on wasm3, an interpreter vendored in `third_party/wasm3` (MIT, `LICENSE` there), built into both web engine copies with the flags `W3F` in `build.sh`: no POSIX host, no thread-local storage (`M3_THREAD_LOCAL` empty, the kernel has no `%fs` segment), no typed function references, stack switching, snapshots or gas metering, and a native stack budget of 1.5 MiB below the entry point (the engine stack is 6 MiB and QuickJS keeps 3 MiB). The JavaScript API is in `web/prelude.js` on top of the natives in `web/wasm.c`.

| Part | How it works |
|---|---|
| Runtime | One wasm3 environment and runtime per page, created on first use; the page arena reset frees it with the page. Every instance is loaded into it as a module named `__iN`. |
| `Module` | The bytes are copied (wasm3 keeps pointers into them) and parsed; a parse error is a `CompileError`. `imports`, `exports` and `customSections` come from a small section reader in the prelude. Function bodies are validated when they are compiled: exported functions when the instance is made, the rest on first call. |
| `Instance` | The import object is checked like Chromium (`TypeError` for a missing module object, `LinkError` for a wrong kind). Function imports become host functions (`CompileRawFunction`) that call the JavaScript value; imported memories, tables and globals are linked by renaming the import to the owning module and export, so wasm3's own linker shares the same object; plain numbers for immutable globals go through `m3_LinkGlobal`. The start function runs inside the constructor. |
| Calls | Exported functions are JavaScript functions with the function index as `name`, the parameter count as `length` and the same object every time (table `get` included). Values convert like the spec: i32 numbers, i64 `BigInt` (a `TypeError` for numbers), f32 rounding, `externref` held in a per-page table, `funcref` as exported functions. JavaScript exceptions thrown in imports come out unchanged; traps are `RuntimeError` with Chromium's messages, stack overflow is `RangeError`. |
| `Memory` | `buffer` is an `ArrayBuffer` over the linear memory itself (no copy). After every call into wasm, at the start of every import call and on `grow`, the engine compares the memory's address and size with the buffer and detaches the old buffer when they changed, so a view of grown memory is never left pointing at freed bytes. |
| `Table`, `Global` | `new` builds a tiny module in memory that declares and exports the object, so it can be imported like an exported one. `get`, `set`, `grow`, `length`, `value`, `valueOf` and `type`. |
| Async | `compile`, `instantiate` (bytes or module), `compileStreaming` and `instantiateStreaming` (any content type is accepted). |
| Long runs | Every 16,384 loop iterations or branches the interpreter asks the engine whether to stop: Escape held in the Browser or more than 20 s in one call stops the code with a `RuntimeError`, and the desktop keeps drawing in between. Found by fuzzing: a module whose inner loop ran one iteration per outer iteration skipped the check when it was tied to the outer loop's back edge. |

Service Workers run on the page thread like the Workers of section 13.39, in a scope object of their own (`self`, `registration`, `clients`, `caches`, `skipWaiting`, `importScripts`, the usual globals).

| Part | How it works |
|---|---|
| Registration | `navigator.serviceWorker.register(url, {scope})` with Chromium's checks (http or https, same origin, scope under the script's directory). The worker script runs, `install` waits for `waitUntil` promises, then `activate`; states `installing`, `installed`, `activating`, `activated` with `statechange` events and `updatefound`; `ready` resolves on activation. Registering a different script for the same scope updates it. |
| Persistence | Registrations (scope, script URL, script hash) are kept in the origin's persistent web storage under a separate namespace (native `storeGet(2, ...)`, stored as origin `sw|<origin>`). On the next visit the page starts controlled: `controller` is set before page scripts run and the worker script is run again on first use; a changed script is installed again. `unregister` removes it. |
| Fetch events | `fetch()` from a controlled page dispatches `fetch` to the worker with a `FetchEvent`; `respondWith` answers it, otherwise the request goes to the network. The worker's own `fetch` goes straight to the network. Subresources the engine loads itself (`img`, `script`, CSS) and `XMLHttpRequest` are not intercepted. |
| Clients | `clients.claim` sets the controller and fires `controllerchange`; `matchAll`, `get`, `postMessage` both ways (`ExtendableMessageEvent` with `source`). |
| Cache API | `caches.open/has/delete/keys/match` and `Cache.match/matchAll/add/addAll/put/delete/keys` with `ignoreSearch` and `ignoreMethod`; only GET, http(s) and non-206 responses are stored. The caches live in memory for the page. |

Other changes in this release: `performance.now()` and event time stamps use the high-resolution timer (0.1 ms steps instead of 10 ms); `Date.now()` advanced only once per second because `k_realtime_ms` in `fs/vfs/dirtree.c` read the CMOS clock on every call instead of keeping its base (now a static); Ctrl+A in the address bar selects the address instead of typing `a`, and other Ctrl or Windows-key combinations no longer insert letters there.

Tested: a WebAssembly test page (40 checks: imports and exports, i64, f32, traps, deep recursion, memory growth from JavaScript and from wasm with buffer detaching, tables, globals, errors, streaming) gives the same output as Chromium on the host and in QEMU except the detail text of one compile error; real WASI programs from the wasm3 test set (`c-ray` ray tracer, `mal` Lisp interpreter) give byte-identical output to Chromium on the host and in QEMU; CoreMark runs at 3,584 on the host (V8 with its optimizing compiler: 30,917). A Service Worker test page (register, ready, controller, fetch interception for GET and POST, Cache API, messages both ways, scope and 404 errors, registrations, unregister) gives the same output as Chromium, and a reload starts controlled, on the host and in QEMU. Mutation fuzzing of three modules under AddressSanitizer found the stop-check gap above and no memory errors.

Not done: a WebAssembly JIT (wasm3 is an interpreter, about a tenth of V8's speed on CoreMark), SIMD, threads and shared memory, tail calls and exception tags in the JavaScript API; Service Worker interception of engine-loaded subresources and navigations, background sync and push; Cache API storage across visits.

## 13.41 Freeze and crash fixes, popover and dialog (v10.17)

Prompted by the owner's report: clicking the Code button on a GitHub repository page froze NoovexOS for 60 s, with the freeze report pointing at `strcmp`.

| Problem | Fix |
|---|---|
| Elements inside a `display:none` subtree kept style pointers into the style pool after a full restyle reset it (only direct children were cleared). Showing or changing them later read freed memory: crashes, or loops over garbage. Found with a random DOM mutation tester that compares the layout before and after a forced full restyle. | Whole hidden subtrees (and shadow roots) are cleared (`unstyle_tree` in `web/dom.c`, used by `web/cssstyle.c`), and every removed node is cleared too, so a detached element never carries a stale style. |
| The HTML parser gave an empty attribute (`class=""`, `value=""`, `id=""`) the text of the previous quoted attribute value, because the reused text buffer was not reset to an empty string. Pages got wrong classes, ids and input values. Found by comparing 150 random selectors with Chromium. | `web/dom.c` uses the empty string when no value text was read. |
| Selector matching with several descendant or sibling combinators retried every ancestor for every inner step, which grows very fast on deep pages with large style sheets. | `match_from` in `web/cssparse.c` returns "fails for all ancestors" and stops retrying (the WebKit approach); results are unchanged. |
| `querySelectorAll` kept at most 1,024 pending nodes and silently skipped the rest of large pages. | Growable work list. |
| `getElementById` walked the whole document every call. | 64-entry cache keyed by root, id and a DOM generation counter that every insert, remove and attribute change bumps (`g_dom_gen`). |
| Long C loops in the engine never gave the desktop a turn, so a slow page froze the whole OS instead of stopping. | `nwe_check_stop` (Escape, desktop pumping) also runs in `querySelector`/`querySelectorAll` and `getElementById`, and stops any engine call that runs longer than 45 s with "This page stopped". |

Popover API: `popover` attribute (`auto`, `manual`, `hint`), `showPopover`, `hidePopover`, `togglePopover` (with `force`), `beforetoggle`/`toggle` events with `oldState`/`newState`, `popovertarget`/`popovertargetaction` buttons, light dismiss (click outside or Escape closes the top auto popover, nested popovers stay), `:popover-open`, and UA styles that hide closed popovers. `<dialog>`: `showModal` (modal styles, `:modal`, focus of `[autofocus]` or the first control, Escape fires `cancel`), `close` with `returnValue` and an asynchronous `close` event, and `requestClose`. The open state is stored in internal attributes (`nvx-popover-open`, `nvx-modal`).

Browser: Ctrl shortcuts use the Ctrl state recorded with each key (`g_key_ctrl` from the keyboard queue) instead of the live key state, so Ctrl+A in the address bar works even when the engine was busy; closing a background tab no longer reloads the current tab; Back addresses kept per tab are no longer cut to 255 characters; navigation and page URLs may be up to 4,095 characters (were 1,023).

Tested: 540 random mutation pages (with shadow roots, clones, hiding and re-showing, moves) give the same layout before and after a forced full restyle and are clean under AddressSanitizer; 150 random combinator selectors give the same counts as Chromium; a GitHub-style repository page with 4,000 rules, 400 rows and the Code menu (popover, tabs, copy button, portal) opens and closes without errors.

Not done: the exact loop on the real GitHub page could not be confirmed here (github.com is not reachable from the build machine); browser look and wider HTML/CSS/JS coverage got no further work in this release.

## 13.42 Mouse, keyboard, clipboard and container queries (v10.18)

Mouse. `mouse_step` in `graphics/ui/input.c` now calls `browser_mouse_down`, `browser_mouse_held` and `browser_mouse_release`, so the engine gets `NWE_OP_DOWN`, `NWE_OP_MOVE` (with the held buttons) and `NWE_OP_UP` instead of one click. The engine passes `button`, `buttons` and `detail` (click count) to JavaScript packed in one key value (`0x40000000 | button | buttons << 4 | detail << 8`), and `__dispatch` in `web/prelude.js` decodes it for `MouseEvent` and `PointerEvent` (`pointerType` "mouse", `pressure`). Pointer capture retargets moves and ups; `getCoalescedEvents` returns the event itself.

| Gesture | Result |
|---|---|
| Drag in a text field | Selection from the press point (`sela` anchor plus `caret`), painted in light blue; `selectionStart`/`selectionEnd` follow. |
| Double-click / triple-click in a field | Selects the word / the whole value. |
| Drag over page text | `__pageSel` builds a DOM `Selection` from the text under the pointer (`text_at`), the highlight is painted by `eng_set_highlight` and `paint.c` (spaces between words included). |
| Drag a range slider | `input` events while moving, `change` on release. |
| Drag a `draggable=true` element | `dragstart`, `dragenter`/`dragover`/`dragleave`, `drop` with a `DataTransfer`, `dragend`. |
| Right-click | `mousedown`/`mouseup` with button 2 and `contextmenu`; if the page calls `preventDefault`, the OS menu is not shown. |
| Middle button | `auxclick` (the OS mouse driver does not report it yet). |

Keyboard in fields: Shift with arrows, Home and End selects; Ctrl with arrows, Backspace and Delete works by words; typing replaces the selection; Up, Down, Home and End move by lines in a `textarea`; `readonly` fields ignore edits; long single-line fields scroll with the caret, and clicks account for that scroll. Ctrl+A, Ctrl+C, Ctrl+X and Ctrl+V use the OS clipboard (`g_clip`, 64 KB, shared with the editor) through `nwk_clip_get`/`nwk_clip_set` and fire `copy`, `cut` and `paste` events whose `clipboardData` the page can read or replace; outside fields they copy the page selection or select all page text. `navigator.clipboard.readText`/`writeText` and `execCommand('copy')` use the same clipboard. Tab and Shift+Tab move focus both ways; Space, Shift+Space, Page Up and Page Down scroll; Enter submits the form.

Keyboard layouts: `drivers/input/ps2/ps2.c` has override tables for Swedish, German, French, Spanish and UK on top of US. `kbd_char` returns Latin-1 characters (for example 0xE5 for a-ring), AltGr (right Alt, or Ctrl+Alt) gives the third level (`@`, `{`, `[`, `\`, currency signs), Caps Lock swaps the case of letters, and the keypad `/` and Enter are mapped. The browser and the desktop take the Latin-1 byte directly; the Terminal passes it to Linux programs as UTF-8. The layout is applied from `/etc/nvx-setup-done` at boot and changed with `kbd_set_layout` (Settings > Language & region > Change).

Container queries: `@container` blocks are parsed at top level and nested (`g_parse_cq`), rules keep their condition in `rule.cq`, and `css_gather` drops a rule whose condition fails for the element. The condition is checked against the nearest ancestor with `container-type` (and the given `container-name`) using its content box from the last layout; `cqw`, `cqh`, `cqi`, `cqb`, `cqmin` and `cqmax` use the same box. When a container's size changes, the engine restyles and lays out once more (`css_cq_changed`).

Sizing: `aspect-ratio` with a set height and automatic width gives width = height x ratio, also for flex row items; `aspect-ratio: auto 4/3` is accepted; extra table width goes to columns without a fixed width first. A sizing page with `vh`, `aspect-ratio`, `fit-content`, `min()`/`max()`/`clamp()`, container queries and percentage heights matches Chromium except rows that depend on font metrics and `min-width: fit-content`.

New JavaScript: `CompressionStream` (stored deflate blocks with gzip and zlib framing) and `DecompressionStream` (real inflate through `N.inflateRaw`), `scheduler.postTask`/`yield`, `TaskController`, `window.event`, `navigator.share`, `canShare`, `userActivation`, `connection`, `CloseWatcher`, `document.getAnimations`, `ToggleEvent`, `PromiseRejectionEvent`, `FormDataEvent`, `CompositionEvent` and other event classes, and interface names such as `TreeWalker`, `CSSStyleRule` and the SVG element types for `instanceof` checks.

Tested: on the host, keyboard and mouse scripts for every row above, 150 DOM mutation seeds with no layout difference, 150 selectors equal to Chromium, the WebAssembly and Service Worker pages, seven app pages with no JavaScript errors and a sanitizer sweep over the page corpus; in QEMU, double-click, slider drag, page text selection, drag-and-drop, right-click, Shift+End, Ctrl+C, Ctrl+V, Shift+Tab and both `/` keys.

## 13.43 Hit testing, form control sizes, intrinsic keywords, middle click, text and file drag, key repeat (v10.19)

Prompted by the owner's photo: on Google's sign-in page the e-mail field could not be clicked and its label sat above the field.

Hit testing (`hit_rec` in `web/layout.c`): a box or text fragment whose style has `pointer-events: none` or `visibility: hidden` is never the target, but its children are still tested (they can turn pointer events back on). Before, such a hit went to the parent element, so a decoration layer over a field (outline and label layers in Material and Google fields) sent the click to the wrapper. Positioned children are tested in `z-index` order (highest first, later in the page first among equals), then in-flow children, then negative `z-index` children.

Percentage heights of inline-level boxes: `measure_atom` gets the definite content height of the block that lays out the line (`g_icbh`), so `height: 100%` on a field or inline block inside a sized container works.

Form controls: text fields are content-box with 2px/3px padding and a 1px border (the same outer size as Chromium's 1px/2px padding and 2px border), no fixed height; width = size x 0.58 em + 1.5 em, height = the normal line height; text areas are cols x 0.6 em + 15px wide and rows x line height high; buttons have 2px/7px padding. The old `size`, `cols` and `rows` width/height hints and the intrinsic aspect ratio of controls are gone.

Intrinsic keywords: `intrinsic_content` measures a box as if its width were `auto`; `clamp_w` uses it for `min-width`/`max-width` with `min-content`, `max-content` and `fit-content`, and flex items use it for the min/max main size. Font metrics follow Chromium: line height = round(ascent) + round(descent) + round(line gap), and one pixel moves from ascent to descent when the descent was rounded down. `ch` uses the width of "0" in the element's font, `lh` the element's line height.

Middle button: `in_btn_raw` counts presses in `mouse_mpress`; the desktop sends a press over the Browser to `browser_mclick`, which sends `NWE_OP_DOWN`/`NWE_OP_UP` with button 3. If the page does not cancel `auxclick` and the target is a link, the engine returns `ret = 2` and the link opens in a new tab.

Text drag: a press inside the current page selection (`__pageSel` checks `Range.comparePoint`) or inside a field selection (`__tdragArm` from the engine) arms a drag instead of starting a new selection. After 5 px the polyfill fires `dragstart` with `text/plain`, then `dragenter`/`dragover`/`drop`; on an editable target that does not cancel `drop` the text is inserted at the drop point (`N.caretAt`, magic 66) with `insertFromDrop` input events, and removed from a source field (`deleteByDrag`).

File drag: dropping a file from the Files app on the Browser calls `browser_drop_file`, which sends the bytes with `NWE_OP_DROPFILE` (16). `__fileDrop` builds a `File` (type from the extension) in a `DataTransfer` (`files`, `items`, type `Files`); if the page cancels `dragover` it gets `drop`, otherwise an `<input type=file>` (or its label) under the pointer receives the file with `input` and `change` events.

Key repeat: `ps2.c` ignores the keyboard's own repeats and repeats the last held key from the timer interrupt (`ps2_kbd_tick`) after `g_krep_delay` ticks every `g_krep_rate` ticks (Slow 700/60 ms, Normal 500/30 ms, Fast 300/20 ms; Settings > Language & region > Key repeat, kept as `krep=` in `/etc/nvx-setup-done`). Modifiers, locks, Escape and hotkeys do not repeat. USB keyboards use the same timing. Repeated keys carry a flag through the keyboard queue (`g_key_rep`), so the browser sends `keydown` with `repeat: true` and no extra `keyup`.

`addEventListener`, `removeEventListener` and `dispatchEvent` called without an object use `window`.

Tested: sizing pages against Chromium (59 + 51 + 19 boxes; remaining differences are the default serif font for `ch` and 3 px on 16 px fields), 80 DOM mutation seeds, seven app pages without errors, host scripts for text and file drops; in QEMU a field under an overlay, held keys, middle click and a text drag.

## 13.44 Connection reuse, parallel fetching, persistent caches, page timing and more interfaces (v10.20)

Prompted by the owner's question why Google's sign-in page is slow. The analysis: the page needs several megabytes of scripts from several servers, and every request paid for a new connection and a new TLS handshake.

Connections (`apps/internet/webglue.c`): requests now go through the TCP code that Linux programs use (`sys/linux/lxsock.c`), which keeps many connections apart, retransmits and advertises a 64 KB window. A pool of 12 connections (`pconn`) is keyed by host, port and scheme; each HTTPS entry has its own `tls_io` buffer and `tls_sess`. Requests send `Connection: keep-alive`; `http_recv` reads exactly one response (`Content-Length`, chunked with `chunk_done`, or until close for old servers) so the connection can be used again. A reused connection that turns out to be closed is retried once on a fresh one. Idle connections are dropped after 50 s, when the server closed them, or when unexpected data arrived. A lost first SYN is resent after 250 ms (`lxs_resyn`). The e1000 and RTL8169 drivers keep 128 receive descriptors instead of 32, so a full 64 KB window from several servers is not dropped.

Parallel fetching: before the HTML is parsed, `prefetch_scan` in `web/engine.c` collects up to 32 `<script src>`, `<link rel=stylesheet>`, `modulepreload` and `preload` (script, style, font) addresses and calls `nwk_prefetch`. That sends up to 6 requests at a time (at most 4 per server) on separate connections, reads the answers in order and keeps them for 60 s (`pf_store`); the normal fetch for the same address then takes the stored answer (`pf_take`) and goes on to the cache as usual. Cookies set by those answers are applied at once.

Caches: compiled scripts (bytecode) and cacheable static files with an expiry time or a validator are written to `webcache.bin` (at most 28 MB, most recently used first, at most once a minute and at power off or restart, never uploaded to the cloud) and read back at the first page after boot; the file starts with the build time so bytecode from another build is ignored. Cached files now carry `ETag` and `Last-Modified`; an expired file is fetched with `If-None-Match`/`If-Modified-Since` and a `304` answer serves the stored copy. Clearing cookies and site data also clears this file.

Time limits: 30 s per script plus 1 s per 100 KB of source (up to 60 s more), 90 s of script time per page, and the engine's stop after 120 s now ignores time spent waiting for the network.

Timing: `about:perf` (`show_perf` in `apps/internet/browser.c`) shows the last page: network totals from `nwk_netstat` (requests, bytes, cache hits, parallel fetches, name lookups, connecting, handshakes, waiting for the first byte, downloading) and engine totals from `nwe_perf`, fetched with `NWE_OP_PERF` (scripts, compiling, styles, layout, painting), plus the time spent moving the pointer while the page was busy (`busy_pump`, measured well under 1%). The status bar shows "Done in ... s: network ..., scripts ..., layout ..." after each load.

Interfaces added in `web/prelude.js`:

| Area | What |
|---|---|
| Web Animations | `element.animate` interpolates numbers with units, colours and transform functions between keyframes (array or object form, offsets, per-keyframe easing) with `duration`, `delay`, `endDelay`, `iterations`, `direction`, `easing` (keywords, `cubic-bezier`, `steps`) and `fill`; `Animation` with `play`, `pause`, `reverse`, `finish`, `cancel`, `currentTime`, `playbackRate`, `finished`, `onfinish`; `KeyframeEffect`, `DocumentTimeline`, `getAnimations`. Values are written to the inline style and restored afterwards. |
| Security | `trustedTypes` with policies, `TrustedHTML`/`TrustedScript`/`TrustedScriptURL` (accepted wherever strings are). |
| Components | `attachInternals` and `ElementInternals` (form value included in `FormData`, `states`, validity, ARIA reflection). |
| Sign-in | `navigator.credentials` (`get` returns `null`, password and federated credentials can be created), `PublicKeyCredential` reporting no platform authenticator and no conditional mediation. |
| Media | `navigator.mediaDevices` with no devices, `MediaStream`/`MediaStreamTrack`, `canvas.captureStream`; a Web Audio graph (`AudioContext`, `OfflineAudioContext` and the usual nodes and params) that keeps time but produces no sound; `speechSynthesis` with no voices. |
| Fonts and canvas | `FontFace` (URL or binary sources, added as `@font-face` rules), `document.fonts.add/delete/has`, `OffscreenCanvas` backed by a hidden canvas. |
| Performance | `PerformanceMark`, `PerformanceMeasure` with marks or options, the navigation entry, paint entries at `load`, `PerformanceObserver` with `buffered`, `performance instanceof Performance`. |
| Other | `URLPattern`, `cookieStore`, `ClipboardItem` with `navigator.clipboard.read/write`, `document.caretPositionFromPoint`/`caretRangeFromPoint` (native `N.textAt`), `window instanceof Window`, elements with an `id` (and named forms, frames and images) as `window` properties through a named-properties object behind the global, `Touch`/`TouchList`, `navigator.getGamepads`, `wakeLock`, `keyboard`, bar objects, more element and SVG filter interface names, WebGL interface names (contexts are still not available), and the missing `on...` handler properties. |

Scroll wheel: the PS/2 IntelliMouse wheel byte and the vmmouse wheel value count a turn towards the user as positive, while USB HID and the rest of NoovexOS count up as positive; both are now negated, so the QEMU pointer and PS/2 mice scroll the right way.

Not done: no JavaScript JIT; the QuickJS interpreter itself was profiled (a virtual-machine style benchmark is about 11 times slower than Node, large bundles are dominated by parsing) but not changed; streaming `fetch` bodies still arrive at the end; WebGL, WebRTC and real audio output are not available.

## 13.45 Faster AES-GCM, TCP window scaling, after-load timing (v10.21)

AES (`lib/crypto/nvxtls.c`): `aes128_enc` now uses one 256-entry table of the combined S-box and MixColumns step (`TLS_TE0`) with rotations for the other three columns and the plain S-box for the last round; the round keys stay in the old 176-byte format, so `nvxwpa.c` (WPA2 CCMP) is unchanged and faster too. GHASH uses a 16-entry table per key (`gcm_tab_init`, `gcm_mul`, the Shoup 4-bit method). Checked on the host against the old code on 300 random keys, nonces, additional data and lengths up to 20 KB, and against the standard all-zero test vector; 6.6 MB/s before, 97 MB/s after.

TCP (`sys/linux/lxsock.c`, `net/stack/tcpip.c`): outgoing SYNs carry the window-scale option (shift 2, `g_tcp_syn_ws`); if the SYN-ACK has one too, our advertised window is the free space of the 256 KB buffer shifted right by 2 and the peer's window is shifted left by its value. Listening sockets keep unscaled windows. `lxs_mv` copies 8 bytes at a time. The e1000 and RTL8169 drivers have 256 receive descriptors.

After-load timing: `nwe_entry` adds every engine call after the load (timer ticks, events, painting) to `after_us`, `tick_us`, `event_us` and `longest_us`; scripts that run after `G.loaded` count in `after_scripts`/`after_js_us`. When `about:perf` is opened from a page, `perf_after` reads those totals and the network counters since the load before the page is replaced.

Network waits: `pc_wait` calls `nwk_busy`, which moves the pointer, keeps the freeze watchdog fed and notices a held Esc (then the wait ends). Prefetching uses a 4 s limit (`g_pc_tmo`) and stops opening connections to a server whose connection or handshake failed in that round; the normal fetch then loads those files.

Engine: 16 MB engine stack (`ESTACK_SZ`), `JS_SetMaxStackSize` 8 MB in NoovexOS (3 MB in the host test program), `JS_SetGCThreshold` 16 MB.

## 13.46 TLS 1.2, P-256 and HelloRetryRequest, faster DOM bindings, feature tests, wheel events (v10.22)

TLS (`lib/crypto/nvxtls.c`): `supported_groups` lists x25519 and secp256r1; the first key share is still x25519 only. `parse_serverhello` fills a `tls_sh` (cipher, group, key, HelloRetryRequest flag, cookie) and requires `supported_versions` 0x0304. A HelloRetryRequest (the fixed SHA-256 random) replaces the transcript with `message_hash` (0xFE 00 00 20, hash of the first ClientHello), adds the HRR, and sends a change_cipher_spec plus a second ClientHello with the requested group and the cookie; a second HRR, an HRR that asks for the group already sent without a cookie, or a ServerHello for another group is refused. The P-256 code is `x509_p256_ecdh` in `nvxx509.c` (Jacobian points with the existing field code, a double-and-add-always loop with masked selection, peer point checked to be in range and on the curve, result wiped). The client change_cipher_spec is sent once. `tls_sess` keeps both application traffic secrets and the key length; `tls_sess_read` handles KeyUpdate (`traffic upd`) and answers `update_requested` with its own KeyUpdate. Session ID: `tls_connect_sess` used `rnd[0..31]` both as the X25519 private key and as the 32-byte legacy session ID, which is sent in the clear, so a passive observer could compute the shared secret. The session ID is now SHA-256 of the client random and the byte 0x53; check.py captures a ClientHello and checks that X25519 of the session ID is not the key share.

TLS 1.2 (`tls12_handshake`): the ClientHello also lists 0x0303 in `supported_versions`, the suites 0xC02B, 0xC02F, 0xCCA9 and 0xCCA8, `extended_master_secret`, an empty `renegotiation_info` and `ec_point_formats`. A ServerHello without `supported_versions` and with legacy version 0x0303 selects TLS 1.2; a server random ending in `DOWNGRD` 01 or 00 is refused. Only the ServerHello itself goes into the transcript; messages that follow it in the same record are handed on. Certificate (1.2 layout), ServerKeyExchange (named curve x25519 or secp256r1, signature over client random, server random and the parameters, checked with `x509_leaf_sig12`: 0x0401/0501/0601 PKCS#1, 0x0403/0503/0603 ECDSA, 0x0804-0806 PSS), CertificateRequest (answered with an empty Certificate) and ServerHelloDone are read from plain records. The premaster secret is the ECDH result; the master secret uses `prf12` (P_SHA256) with "extended master secret" over the session hash when the server echoed the extension, else "master secret" over the randoms; the key block gives 16-byte AES or 32-byte ChaCha keys and 4-byte (GCM salt) or 12-byte IVs. Records (`t12_write`, `t12_open`): GCM uses the salt plus an explicit 8-byte sequence number in front of the ciphertext, ChaCha20-Poly1305 the IV XOR the sequence number; the additional data is sequence, type, version and length. Finished messages are 12 bytes of `prf12` over the transcript hash. `tls_sess.v12` switches `tls_sess_write`/`tls_sess_read` to these records; a HelloRequest is answered with a no_renegotiation warning; `tls_get` now reads through `tls_sess_read`.

Host tests in `tools/tests/tls/x509h.c` (`ecdh` and `get` modes, the last arguments are PEM files to trust) are run by check.py against the cryptography module and `openssl s_server`: TLS 1.3 with X25519, P-256 and an RSA certificate, TLS 1.2 with the four suites, and a proxy that changes the last byte of the ServerKeyExchange, which must be refused.

DOM speed: `qsel` in `web/cssparse.c` keeps 64 parsed selectors keyed by root and an FNV hash; results are reused while `g_dom_gen` is unchanged and the selector has no `:` (state pseudo-classes can change without a DOM change). `lc`, `ieq` and `is_ws` are inline in `nwe.h`; `attr_get` compares the first letter before calling `ieq`. Native 69 (`clsOp`) does `contains`/`add`/`remove`/`toggle` on a token attribute in C (duplicates removed, an unchanged `toggle` does not rewrite the attribute, -1/-2 for an empty or spaced token so the right DOMException is thrown). `attr_set` records a 256-bit hash set of `on...` attribute names; native 70 (`onSeen`) lets `run()` skip `inlineHandler` for event types no element has. `kebab` and `dsKey` results are cached in maps.

Sort (`third_party/quickjs/quickjs.c`): `js_array_sort` calls `vs_natural_sort` (runs, reversal of strictly descending runs, binary insertion up to 24 elements, bottom-up merging into a temporary array, a merge is skipped when the two runs are already in order); if memory runs out it falls back to `rqsort`. The compare function still breaks ties by position, so the result is stable either way.

Ring-3 waits (`sys/native/syscall.c`): syscalls 51 (wait for a key) and 71 (sleep) set `g_frz_hold` while they wait and restore it after.

Feature tests: the pages `es.html`, `dom.html` and `b3.html` (kept with the build scripts) list each check as name, function and expected value; Chromium 141 and the host test program both print the failures. Chromium's two failures are its own (`SharedArrayBuffer` needs cross-origin isolation there, `Math.sumPrecise` is not in 141).

New interfaces (`web/prelude.js`, last block): live collections are a `Proxy` over an array that is refilled when `domGen` changes; form validity follows the HTML rules for barred elements (disabled, read-only, hidden, buttons), with Chromium's messages; `toDataURL` writes an uncompressed (stored deflate) RGBA PNG with CRC-32 and Adler-32; `cssRules` splits the sheet text at top level into rule objects with `selectorText`, `style`, nested `cssRules` for `@media`/`@supports`, and `insertRule`/`deleteRule` rewrite the text; `img.width` of an element that is not rendered returns the attribute, then the natural size (`web/js.c`).

Horizontal scrolling: `box.sx` is the horizontal scroll offset (the field already existed and was unused) and `node.scrollx` keeps it across layouts (`build_box`, `apply_scroll_offsets`). `box_scroll_w` walks the box's descendants (not into boxes that clip horizontally, not into fixed boxes) for the right-most edge of boxes and fragments plus the right padding. Painting (`paint_box`, `sub_bounds`, absolute boxes, the focus ring), hit testing and caret positions subtract `sx` like `sy`. `box_rect` now subtracts the scroll offsets of the ancestors (it did not, so `getBoundingClientRect` was wrong inside any scrolled box); `box_rect_s(..., 0)` keeps the unscrolled position for `offsetLeft`/`offsetTop`. `scrollLeft` reads `node.scrollx` and clamps writes to 0 .. scroll width minus client width; `scrollWidth` uses `box_scroll_w`. A wheel or box scroll fires a non-bubbling `scroll` event at the element; a change of `scrollTop`/`scrollLeft` from a script queues the element (`eng_queue_scroll`, 16 entries) and the next tick fires its `scroll` event. Both setters lay out first (`js_flush_n`) and clamp to the scroll range when the layout is up to date; otherwise `apply_scroll_offsets` clamps after the next layout.

Wheel: `do_wheel(x, y, dy, dx)` dispatches `wheel` at the element under the pointer (deltaY in the low 16 bits of the key, deltaX in the high 16) and scrolls only if it was not cancelled: `dx` scrolls the nearest box with `overflow-x: auto`/`scroll` that can move, `dy` as before. Listeners keep a `passive` flag (default true for wheel and touch listeners on `window`, `document`, `html` and `body`); `preventDefault` does nothing while a passive listener runs. `browser_wheel` turns Shift+wheel into `deltaX`; `browser_wheelxy` also takes the horizontal amount the GUI loop now reads from `mouse_hscroll` (touchpads and PS/2 mice). `ps2_mouse.c` sends the 200-200-80 rate sequence after the wheel mode and uses ID 4 (IntelliMouse Explorer); `mouse_feed` decodes the fourth byte like Linux (`0x40` horizontal, `0x80` vertical, else wheel nibble plus buttons 4 and 5). `in_xbtn` counts back/forward presses (PS/2 and USB boot or report mice); a back press goes back in the browser.


## 13.47 Request headers, animation events, frame load events (v10.23)

Request headers (`apps/internet/webglue.c`): `g_nwk_kind` tells `req_head` what kind of request it builds (1 page load, 2 script request, 0 other) and `g_nwk_xhdr` holds the page's own header lines. For kind 2 the page's headers are copied after removing CR and LF and dropping the forbidden names (`xh_forbidden`: `Host`, `Cookie`, `Content-Length`, `Content-Type` (sent separately), `Connection`, `Origin`, `Referer`, `User-Agent`, `Proxy-*`, `Sec-*` and the rest of the Fetch list); `Accept: */*` is added unless the page set one; `Sec-Fetch-Site` (`same-origin`, `same-site` by registrable domain from `reg_dom`, else `cross-site`), `Sec-Fetch-Mode: cors` and `Sec-Fetch-Dest: empty` are sent; `Origin` is sent for posts and cross-origin requests. Kind 1 sends `Sec-Fetch-Mode: navigate`, `Sec-Fetch-Dest: document`, `Sec-Fetch-User: ?1` and `Upgrade-Insecure-Requests: 1`. `browser.c` sets `g_nav_from_page` when a page started the navigation; a typed address sends no `Referer`. `nwk_fetch_real` copies the response header block to `nwk_resp.hdrs` for kind 2.

Scripts (`web/js.c`, `web/prelude.js`): `fetch` takes a sixth argument (the header block, from `hdrBlock`) and returns the response headers as a fifth element, parsed by `parseHdrs` into the `Headers` of the `Response`. `XMLHttpRequest` checks the state in `setRequestHeader`, joins repeated names with ", ", fills `status`, `statusText` (from `STATUS_TEXT` when the server sent none), `responseURL` and the headers, fires `loadstart`, `progress`, `load`/`error`, `loadend` (and the upload events when there is a body) and sets `readyState` 4 on errors. The host test program `tools/webhost.c` can fetch from `127.0.0.1`, which is how the test server checks the headers.

Animation events (`web/anim.c`): queued events carry the event name, the animation or property name and the elapsed time, and duplicates in one frame are dropped. Transitions fire `transitionrun` when created and `transitionstart` after the delay, `transitioncancel` when the element gets `display: none`, is removed or gets a new transition for the same property; `animationcancel` fires when an animation is removed. Property names come from `css_prop_names`. Snapshots skip nodes with `NF_NOTRANS` (512), which Web Animations set while they apply their own values so their end does not start a CSS transition. `engine.c` passes each event to `js_anim_event`, which builds a real `AnimationEvent` or `TransitionEvent` and lets it bubble.

Frames: `frameScan` runs every 40 ms and fires one `load` event per frame for each `src` or `srcdoc` value. For an empty frame, `contentDocument` is a document from `N.parseDoc` with `open`, `write` and `close`, and `contentWindow` an object in the same realm with `document`, `parent`, `top`, `eval` and a `postMessage` that delivers to its own listeners.

## 13.48 Name lookups, frames, streaming, faster built-ins (v10.25)

Lookups (`net/stack/tcpip.c`): `dns_try` and `arp_resolve` poll the card directly; frames that are not their answer now go to `lxs_frame_ext` (the socket layer) instead of being dropped. A query is resent after 100 and 200 ticks and a server is given 3 s. The cache has 64 entries with an expiry from the answer's TTL (at least 60 s, at most 1 h). `pc_get` writes `host d<ms> t<ms> s<ms> r<n>` to the network log for each new connection and keeps the slowest in `g_nwk_slow`; `pc_wait` calls `nwk_busy` always, which beats the main loop so long downloads do not trigger the freeze report.

Frames: `N.frameOpen(url, srcdoc)` (native 58) hands a `srcdoc` document to `nwk_frame_open_doc`, which loads it in the frame engine like a fetched page; `frameScan` picks the first `iframe[srcdoc]` or `iframe[src]`.

Streaming: during a page fetch (`g_nwk_kind` 1) `http_recv` calls `stream_try`, which (at most twice, after 0.3 s and 1.5 s, with at least 2 KB new) de-chunks the bytes so far, inflates gzip with `pz_inflate_part` (returns what it could decode) and calls `g_nwk_stream`. The browser loads that HTML with `btn` 2: the engine parses without scripts, lays out and paints; `g_nwk_offline` makes fetches answer only from the cache.

QuickJS: `js_regexp_replace_fast` runs `lre_exec` directly for standard regular expressions without sticky flag or named groups when the replacement is a function or a string without `$`, collects all matches first and then calls the function with the captures, position and string. `map_hash_key` finishes with a 32-bit mix and the table grows at one entry per bucket. `json_quote_to` writes quoted strings in runs; arrays skip the index string and `js_json_check` for primitives when there is no replacer; numbers, booleans and null are written directly. After a collection the next one starts at twice the live size (at least the value given to `JS_SetGCThreshold`).

## 13.49 Script order, parallel late downloads, prelude bytecode, faster built-ins (v10.26)

Script order: `eng_node_inserted` (DOM insertion from JavaScript) used to skip scripts while `G.parsing` was set, so scripts added by an inline script during the parse were lost. `on_ins` now runs a script without `src` at once when it has text (`run_script_node`; an empty one is left for later), and queues one with `src` in `pend_scripts`. `do_load` runs the queue after the parse and before the `defer` list and `js_fire_load`; queued scripts added later run at the next tick. `js_run_script_in` keeps the deadline of an outer script that is running (nested runs used to clear it).

Parallel downloads: `prefetch_nodes` collects the `src` of queued scripts and calls `nwk_prefetch_more`, which, unlike `nwk_prefetch`, keeps earlier results, skips addresses it already holds and replaces the oldest entry when all 32 are used. The tick also prefetches up to 6 pending images and `prefetch_links` (style sheet gathering) up to 8 style sheets that have not been fetched yet.

Prelude: `js_init` looks up the compiled prelude in the code cache (`nwk_code_get` with a key from its length and a sample of its bytes); the first time it compiles with `JS_EVAL_FLAG_COMPILE_ONLY`, stores `JS_WriteObject` bytecode and runs it.

QuickJS: `json_obj_fast` writes plain objects from their shape (string keys only, data properties, no replacer list); `js_array_every` reads fast arrays directly and appends `map`/`filter` results with `add_fast_array_element`; `js_append_enumerate` copies fast arrays and `arguments` without making an iterator when the array iterator is unchanged; `json_parse_string` returns plain ASCII strings without a string buffer; `json_parse_number` and `next_token` read integers of up to 9 digits directly. Before an assignment expression that starts with `(` is checked for an arrow function (`js_parse_skip_parens_token`, which tokenizes up to the matching `)`), `arrow_params_possible` peeks at the next token: only `)`, `[`, `{`, `.`, an identifier, `of` or `await` can start parameters, so `(function`, `((` and similar are parsed at once. Regression check: 3000 scripts from npm packages compiled with the v10.24 and the new QuickJS give byte-identical bytecode and the same errors.

Busy box: `web_busy_ms` gives the time of the running engine call; `busy_pump` draws the box into the scene and presents its rectangle once a second after 3 s.

## 13.50 Error page, honest status, cut-off downloads (v10.27)

Error log: `webglue.c` keeps a ring of 64 entries of up to 400 characters with a time (`nwk_error`, `web_err_count`, `web_err_get`, `web_err_total`). `nwk_error_mark` adds a "Page <address>" line from `browser_go`. The `nwk_fetch` wrapper adds a line for every failed request when the browser is online: the return code picks the text (-100 name lookup, -2 connect, -5 secure connection, -4 cut off, -3 no answer), a status of 400 or more gives "HTTP <status>". In `js.c`, `report` (uncaught errors with their stack) and `console.error` (level 2) or `reportError` (level 3) also call `nwk_error`; the host promise rejection tracker keeps up to 32 rejected promises without a handler and logs those still unhandled at the start of the next `js_tick`. `show_errors` in `browser.c` builds `about:errors` from the ring.

Status: `run_script_node` sets "Running script <name>" before a script runs and compares the error counter `g_js_errs` after it. `nwk_status` in `browser.c` changes a plain "Done" into "Done, N errors - see about:errors" when the error total has grown since the page started.

Cut-off downloads: `http_recv` returns whether the body was complete. `http_once` retries once when it was not, then returns -4; the prefetcher only keeps complete results.

Compiling: `get_closure_var` scans up to 23 closure variables; above that it keeps an open-addressing hash of (`var_idx`, `closure_type`) in the function definition (`cvh`), filled in for new entries on each lookup (entries never change once added) and freed with the definition.

## 13.51 Google sign-in checks, long addresses, locales (v10.28)

Token lists: `DOMTokenList.prototype.supports` looks up the owner element and attribute: `rel` on `link` uses the link types Chromium supports (`preload`, `modulepreload`, `preconnect`, `dns-prefetch`, `prefetch`, `stylesheet`, `icon`, `manifest`, `alternate`, `next`, `search`, `expect`, `compression-dictionary`), `rel` on `a`, `area` and `form` uses `noreferrer`, `noopener` and `opener`, `sandbox` uses the 14 sandbox flags and `blocking` uses `render`. Any other list throws a `TypeError`, as in Chromium.

Reflected properties: a block after the iframe code in `prelude.js` adds string, boolean, URL, integer and enumerated reflections with five small helpers (`rS`, `rB`, `rU`, `rI`, `rE`); each one is only added when the property does not already exist anywhere in the prototype chain (`findDesc`), so native getters win.

Blank frames: the `contentWindow` of an `about:blank` frame copies every other own property of the page's global object (data properties as values, accessors as getters that read the page's value) after the fixed list.

Addresses: `url_resolve` and `norm_path` in `util.c` use static 8 KB buffers (the functions are not reentrant and the engine runs on one thread), so relative addresses and query strings are no longer cut. Buffers of 1 and 2 KB for addresses in the engine, the style parser, the network layer (`purl`, request lines, redirects, cache entries, frame addresses), `nwk_resp.final_url` and the browser (`g_url`, `g_cururl`, the back stack, tabs, the resolved address) are now 4 KB; `sset` in `browser.c` stops at 4095 characters.

Locales: the `Intl` replacement has a table `LT` of separators, date order and separator, zero padding, 12 or 24 hours, currency placement and names. `NumberFormat.format` maps the US result to the locale's separators (Spanish and Polish do not group four-digit numbers); `DateTimeFormat.formatToParts` hands every non-US locale to `locParts`, which builds numeric or named dates and times from the table.

Layout: a `<button>` that is a block box and taller than its content moves its fragments and children down by half the difference. `list-style-type: disclosure-closed` and `disclosure-open` are new marker types (`LS_DCLOSED`, `LS_DOPEN`) drawn as triangles; an `inside` marker now moves the first line's text to the right. An absolutely positioned box with `left` and `right` and `width: fit-content` (or `max-content`, `min-content`) is no longer stretched, so auto margins center it.

## 13.52 about:pending, two frame engines (v10.29)

Pending list: a block at the end of `prelude.js` (main page only) wraps `fetch`, `XMLHttpRequest.prototype.send`, `setTimeout` (timers of 1 s or more), `setInterval` and `clearTimeout`/`clearInterval`, listens in the capture phase for `transitionrun`/`end`/`cancel`, `animationstart`/`end`/`cancel` and `load`/`error`, and once a second (and on `pagehide`) builds a text with one line per item; lines starting with `@`, `=` and `#` are the address, the state line and section titles. `N.pendingSet` stores it in `webglue.c` (12 KB, with the time), `browser_go` clears it when a real page starts, and `show_pending` in `browser.c` turns it into `about:pending`.

Frame engines: `build.sh` links the engine three times: the page engine, `nwe2_entry` and `nwe3_entry` (each copy has its own globals, about 2 MB of static data, and its own memory arena; the third may take up to 128 MB). In `webglue.c` the frame state is an array of two slots (`fslot`: address, `srcdoc`, state, position, paint buffer, request and two message queues); `F` points at the slot in use, the old `g_frame_*` names are macros on it, and `nwk_frame_slot` (page script `N.frameSlot`) chooses the slot for the calls that follow. `frame_call` calls the slot's engine; `frame_tick`, `frame_hit` and `frame_mouse` go over both slots; `nwk_frame_paint` uses the slot whose address matches. The copies' own `framePost`/`frameRecv` are renamed to slot-specific queue functions (`nwk_frame_post_up3`, `nwk_frame_recv_down3` for the second). To keep the image under the 0x6000000 installer limit, the Doom WAD is linked as `doom1.wad.xz` and unpacked with `xzk_decode_into` when Doom starts.

Frames: `FR` in `prelude.js` holds two slots (`S`), each with the element running there (`run`), its key (address or `srcdoc` length), whether it has loaded, and the time of the last message; `want` lists the frames the page has sent messages to. `FrameWindowProxy.postMessage` posts at once only to the running, loaded frame; otherwise the message is kept on the element (`__fq`). `frameScan` (every 40 ms) watches `N.frameInfo` (the frame engine state: 2 loaded, 3 failed) to fire the running frame's `load` once and flush its messages, restarts the frame when its address changes, and switches (`N.frameSwitch` resets the frame engine, then `N.frameOpen`) to a wanted frame when the running one has been quiet for 1.5 s or failed. Frames that do not run still get an immediate `load` as before. In the frame engine the three new kernel functions are redefined to no-ops (`build.sh`).

Animation limits: `SNAP` (the values before a style pass) holds 4096 entries and is allocated with `nwk_alloc_arena` on first use, because the kernel's static area (`__bss_end`, 0x18000000) cannot hold another 2 MB for each of the two engines; `EV_MAX` is 512 and `anim_events` keeps what it could not return.

Time zone: `nwk_tz_min` computes the offset of the setup time zone (`nvx_tz_index`) for the current time, with EU summer time (last Sunday of March to last Sunday of October, 01:00 UTC) and US summer time (second Sunday of March to first Sunday of November, 02:00 local); `localtime_r` in `krt.c` and QuickJS dates use it.

Import maps: `im_load` in `js.c` walks the document once per page (reset in `js_init`) for `script[type=importmap]`, parses it with `JS_ParseJSON` and keeps up to 256 `imports`; `mod_normalize` maps the specifier (exact match, then the longest prefix ending in `/`), then the resolved address.

## 13.53 Promise tracking, code snippets for errors, Content-Length (v10.30)

Promises: the pending block in `prelude.js` wraps browser functions with `patchM` (methods) and `patchG` (getters such as `document.fonts.ready`); a wrapper hands the result to `regP`, which keeps the label (with the first string argument, for example the element name of `whenDefined`) and the time until the promise settles (at most 300 entries; a promise is only counted once). `IntersectionObserver`, `ResizeObserver`, `MutationObserver` and `PerformanceObserver` are replaced by subclasses that count `observe` calls and callbacks; only intersection and resize observers are listed when they never called back (they must report once after `observe`). `requestIdleCallback` is wrapped to list callbacks that have not run.

Code snippets: `run_script_node` records the address and text of every external script (`eng_src_add`, 256 per page, reset in `do_load`; the text lives in the page arena). `js_err_snippet` in `js.c` takes the first `(address:line:column)` of a stack, finds the text (`eng_src_get`), and adds a line with up to 70 characters before and 90 after the column to the error ring. It runs for uncaught errors (`report`) and for errors reported by the page script (`reportErr`, level 3).

Content-Length: `nwk_fetch_t` in `webglue.c` adds `Content-Length` for every method starting with P or D, 0 when there is no body; `Content-Type` only when the page or the body gives one. `bodyType`/`bodyRaw` in `prelude.js` encode `FormData` as multipart with a random boundary kept on the object (`fdBound`, `fdBytes`).

New APIs: a block before the pending block in `prelude.js` (MediaQueryList and its event, IdleDeadline, LockManager and Lock, MediaSession, StorageManager, ScreenOrientation, PerformanceTiming and PerformanceNavigation, VideoPlaybackQuality, MediaList, MediaCapabilities). `MediaQueryList` objects with a `change` listener or `onchange` are checked on every `resize` and once a second.

Elements: `layout_table` lays captions out from the border-box top and moves the rows below them; `paint_bg_border` draws the table background and border from the bottom of the last caption. Ruby uses the default styles `ruby{display:inline-flex;flex-direction:column}` and `rt{order:-1;font-size:50%}` (an older `ruby{display:inline}` rule later in the default sheet was removed). `flatten` in `layout.c` marks the piece before a `<wbr>` as a break opportunity. `attr_set` in `dom.c` calls `details_exclusive` when `open` is set on a `<details>` with a `name`. `paint.c` draws a moving segment for a `<progress>` without `value` and sets `g_prog_indet`, which makes the engine tick repaint every 50 ms.

## 13.54 Unfired events, hit testing, loading indicators, messaging (v10.31)

Unfired events: the pending block in `prelude.js` wraps `EventTarget.prototype.addEventListener`. The first time a type is registered, a capture listener for it is added on `window` that records that the type was fired. `about:pending` lists the types that were registered but never fired, with the target and the time since registration; types matching `USER_EV` (input, focus, pointer, page lifecycle and other rare events) are grouped on one line, because a page normally waits for them.

Hit testing: "What is under the mouse" calls `elementFromPoint` at the centre of the page and three other points. For each hit it shows the element, its position, z-index and opacity, and the first ancestor with `inert`, `aria-busy`, `aria-disabled` or `pointer-events: none`. Fixed or absolute elements covering more than half of the viewport are listed as overlays; the focused element and `document.hasFocus()` close the section.

Loading indicators: elements with `role=progressbar`, `progress` elements and elements whose class contains "progress" are listed with their computed display, visibility, opacity, size and classes. A `MutationObserver` (`pbWatch`) on each indicator and its parent, attributes only, records when the classes last changed, so a bar that is never told to stop shows "classes not changed since first seen".

Load order: `do_load` in `engine.c` calls `js_fire_stage(1)` after parsing (`readystatechange` to "interactive"), runs the pending and deferred scripts, calls `js_fire_stage(2)` (custom element and document flushes, `DOMContentLoaded`), sets `G.loaded` to 2 and calls `js_fire_stage(3)` ("complete", `load`, `pageshow`). `__fireLoad` still runs stages 2 and 3 for callers that load without the split.

Messaging: `G.postMessage` always sets `origin` to `N.origin()` and drops a message whose target origin (parsed with `URL`) differs; `framePoll` compares the target by origin too. `MessagePort` extends `EventTarget`; messages wait in `_q` until `start()` or an `onmessage` handler, and transferred ports arrive in `e.ports` (`MessageEvent` copies `ports` and `lastEventId` from its init). `BroadcastChannel` keeps the open channels per name in `BCH` and delivers a structured clone to every other open channel.

Fonts: `document.fonts.load(spec)` takes the family list after the size in `spec`, loads the faces of the set with those families and returns them; `loading` and `loadingdone` (or `loadingerror`) are dispatched through the small listener table on `document.fonts`, which also calls the `on...` properties.

List boxes and dialogs: `sel_listbox_rows` (`size` above 1, or 4 for `multiple`), `sel_items` and `sel_item_h` in `layout.c` size a list box from its rows and the widest option or group label. `paint.c` draws the rows, group labels and the selected options; `listbox_click` in `engine.c` selects the row under the pointer (in a `multiple` list it replaces the selection; Ctrl-click to add is not done yet) and fires `input` and `change`. A modal dialog (`nvx-modal` with `open`) gets `height: fit-content` and a translucent rectangle over the page before it is painted; `layout_abs` no longer stretches boxes whose height is `fit-content`, `max-content` or `min-content`.

Error reports from pages: `G.__repSeen(url, body)` in `prelude.js` is called from the wrapped `XMLHttpRequest.open`/`send`, `fetch`, `navigator.sendBeacon` and the `src` setter of `img` elements. For addresses containing `jserror` or a CSP report path it reads `error`/`message`, `script`/`url`, `line` and `trace`/`stack` from the query or a form-encoded body and logs one entry per distinct message (at most 41) with `N.log(..., 4)`. Level 4 in `js.c` goes to `about:errors` without the "Uncaught" prefix and without a code snippet. `sendBeacon` now posts the data with `fetch` (`keepalive`, `credentials: include`, `no-cors`).

## 13.55 Old event names, attributes, element properties, XPath, scroll snap (v10.32)

Old event names: `invoke` in `prelude.js` checks trusted `animationend`, `animationiteration`, `animationstart` and `transitionend` events (`LEGACY_EV`). If the node has no listener and no handler for the event's own type (listeners wrapped with `nvxL`, the browser's own trackers, do not count), the listeners and the `on...` handler for the old name (`webkitAnimationEnd` and the others) run with `event.type` set to that name. `invokeIn` holds the old listener loop.

Attributes: the native `attributes` getter (array of name/value objects) is wrapped. `mkMap` returns a `Proxy` over a `NamedNodeMap` prototype, cached per element in `MC`; indices and attribute names resolve through `attrOf`, which looks the name up exactly, then lower-cased, and returns the cached `Attr` for that element and name (`AC`). `Attr` objects keep `_e` (owner) and `_v` (last value); writing `value` calls `setAttribute`. Names starting with `nvx-` (internal markers such as `nvx-modal` and `nvx-design`) are left out. `SVGN` restores the case of SVG attribute names that the parser stores lower-cased.

Element properties: the native accessors on `Element.prototype` in `elem_funcs` (`js.c`) are copied by the `MV` table to the tag prototypes that have them in Chrome and deleted from `Element.prototype` (`G.__elemOnly` keeps the descriptors). Tags without a native tag id (`col`, `slot`, `track` and others) get their prototype from `tagproto_nm` in `js.c`, filled by `N.setProto` with the tag name.

XPath: a tokenizer, a recursive parser for the XPath 1.0 grammar and an evaluator over the live DOM (attribute nodes are the `Attr` objects above; node sets are sorted with `compareDocumentPosition`). HTML element names match without case. `document.evaluate`, `document.createExpression`, `XPathEvaluator`, `XPathExpression` and `XPathResult` (all ten result types with the type checks Chrome makes) use it. Variables and namespaces are not supported.

Design mode: setting `document.designMode` to "on" puts `nvx-design` on the document element; `edit_host` in `engine.c` returns the body when it reaches such a root, so clicks place the caret and typing edits the whole document. `isContentEditable` (`G_ISCONTENTEDITABLE`) is `edit_host(n) != 0`.

Scroll snap: `scroll-snap-type`, `scroll-snap-align` and `scroll-snap-stop` are kept in the style (`snaptype`, `snapstrict`, `snapalign`, `snapstop`) and returned by `getComputedStyle`. In `do_wheel`, a scroll box (or the page, from the document element's style) with block snapping collects the snap positions of its descendants (`snap_collect`, not inside nested snap containers) and `snap_pick` moves to the next position in the scroll direction (mandatory) or to the nearest one within 30% of the viewport (proximity). `scrollend` is dispatched after each wheel scroll of a box and after the page's `scroll` event.

Web cache file: `wc_save` writes the stamp, then the payload length and a 64-bit FNV-1a hash of the payload, then the entries; `wc_load` ignores the file when the length or the hash do not match.

## 13.56 The desktop keeps running during page loads (v10.33)

Engine calls run on the engine stack (`nwe_call_stack`). While a script runs, QuickJS calls `interrupt_cb`, network waits call `nwk_busy`, and style, parsing and now `lay` (every 128th box) call `nwe_check_stop`, which calls `nwk_busy`. When `g_web_pump_ok` is set and the engine is not already nested, `nwk_busy` runs `desk_nested_step` at most every 30 ms: pointer and touchpad polling, ACPI and sound polling, media, input handling (`process_input`) and one full `compose_present`. The time spent there is subtracted from the engine's clocks (`g_susp_total`, `nwk_ms_active`), so it does not count against the 60 s script and 300 s job limits. Without `g_web_pump_ok`, `busy_pump` only moves the pointer and draws the "This page is busy." box after 3 s.

`g_web_pump_ok` is now set for page loads (`web_load_html`) and for navigations that a page tick asks for (`web_process_nav` in `browser_tick`), in addition to ticks and queued input (`bq_drain`). Two guards keep the nested step safe wherever an engine call starts: `g_comp_depth` (`compose_present`) makes it return at once inside a compose, and `g_pi_depth` (`process_input`, lowered around the `browser_tick` call at its end, where nesting was already allowed) makes it only move the pointer and compose when the engine was entered from input handling (for example the Terminal's `browse` command). During a nested step the browser window shows its saved picture (`pc_show`) and browser input is queued (`bq_push`) until the engine returns. `close_win(W_BROWSER)` sets `g_busy_esc` while the engine is busy, which stops the page like Esc.


## 13.57 Chrome-size cookies, long waits and more redirects (v10.34)

The cookie jar (`net/http/cookies.c`) holds `CK_MAX` 3000 entries. A cookie's value is a separate block (`value`, `vcap`) that `ck_val` grows when a longer value arrives; a slot keeps its block when it is reused. A `Set-Cookie` whose name and value together exceed `CK_VMAX` 4096 bytes is refused, as in Chrome. Before a new cookie is stored, the cookies of the same site (the last two labels of the domain, `ck_site`) are counted; at `CK_PER_SITE` 180 the oldest of them gives up its slot, otherwise a free slot is used, otherwise the oldest cookie overall. `cookie_header` sorts by path length and then by creation time, and skips a cookie that does not fit instead of stopping. Up to 16 KB of cookies go into a request (`req_head`), the WebSocket handshake and `document.cookie`; the request buffers of `nwk_fetch_real` and `nwk_prefetch` are 64 KB, so the largest header (address, page headers, cookies, referrer) always fits. `tools/tests/cookies/ck_t.c` includes the real file and is run by `check.py --only cookies`.

`pc_rd` picks its wait from `pc_rd_limit`: 300 s for script requests (`g_nwk_kind` 2), 120 s for page loads (1) and the old 20 s otherwise; prefetches keep their short limit. Every 45 s without data it calls `lxs_keepalive`, which sends an ACK one byte behind `nxt` when nothing is unacknowledged; the server answers with an ACK, which keeps NAT entries alive and shows a dead connection. `nwk_fetch_real` follows up to 40 redirects. The script time limit in `web/js.c` now uses `js_clk`, the active time minus the time spent in network fetches (`nwk_net_us`), so a script that waits for the network is not stopped for it.

## 13.58 Background requests, services, WebGL and CSS 3D (v10.35)

Background requests. `apps/internet/webglue.c` runs script requests on fibers: `struct nfib` with a 512 KB stack (`NF_STK`, a guard page at the bottom), up to `NF_N` 16 at once, switched by the small assembly routine `nf_swap`. When a fiber resumes, `nf_resume` restores the per-request globals (`g_nwk_kind`, `g_nwk_xhdr`, `g_pc_tmo`, `g_pc_abort`, `g_tls_scr`, the stream state). The TLS code keeps its buffers in `struct tls_scratch`, reached through `g_tls_scr`, so each fiber has its own. Inside a fiber `pc_wait` yields instead of spinning. The request table `nreq` holds `NQ_N` 96 requests, at most 6 per host, bodies up to `NQ_MAXBODY` 48 MB; `nf_recv` and `nf_body` decode chunked bodies (with trailers) as data arrives and `nq_decode` unpacks gzip in steps. The page engine sees `nwk_net_start`, `nwk_net_event`, `nwk_net_abort`, `nwk_net_cancel_owner`, `nwk_net_active` and `nwk_net_list`; `browser_tick` calls `nwk_net_step`, which runs the fibers for up to 4 ms. In `web/js.c` the natives `netstart`, `netpoll` and `netabort` connect this to `fetch` and `XMLHttpRequest` in `web/prelude.js`: `fetch` resolves at the headers and gives a lazy stream for the body; XHR reports `readyState` 3 with the text so far. A new page cancels the old page's requests by owner.

Services. `apps/system/services.c` keeps a table of services (`net`, `downloads`, `power` required; `webcache`, `cookies`, `clock`, `wifi` optional). `svc_tick` runs from the main loop; the stopped set is saved as `svcoff=` in the settings file. The clock service sends an NTP request over UDP, sets the system time with `k_realtime_set` and the hardware clock with `write_clock`, and repeats every 6 hours. Downloads (`g_dl[8]` in `apps/internet/browser.c`) run on the same request fibers with the owner `DL_OWNER`; progress shows in the browser and on the taskbar.

Security. A frame's `sandbox` flags travel from `frOpen` in the prelude to `nwk_frame_sandbox` and into the frame request key; `web/engine.c` sets `G.sandbox`, uses the origin `null`, does not start scripts without `allow-scripts` and blocks forms without `allow-forms`; the prelude blocks dialogs, pop-ups, storage and cookies. `cookie_header2` applies `SameSite`. `eng_mixed` blocks or upgrades insecure loads on HTTPS pages. CSP: `csp_parse`, `csp_check` and `csp_allow` in `web/engine.c` (with their own SHA-256 for hashes), fed by `nwk_doc_policy`; `frame_refused` applies `frame-ancestors` and `X-Frame-Options`. `mm_guard` in `mem/pmm_vmm.c` unmaps one page (splitting a 2 MB page when needed) below the engine stack and the fiber stacks and after the page memory.

WebGL. `web/webgl.c` is a software WebGL 1.0. Shaders go through a preprocessor (`#define` with arguments, `#if` expressions, `#ifdef`), a lexer and a recursive parser into a syntax tree, and the code generator emits register bytecode (`ginst`: operation, size, destination and two or three operands, all offsets into one float array per shader). Variables and temporaries sit at the bottom of that array and constants at the top; after compiling, `peep` removes a copy when the instruction before it can write straight into the destination (only for temporaries, never at a jump target), and the array is cut down to the used part. Each function gets its own range of temporaries, so a call cannot overwrite the caller's values. `run_sh` interprets the bytecode with a fast path for one-component operations and a step limit. Linking matches varyings, assigns attribute locations and collects uniforms; each program gets its own copy of both shaders, so uniforms belong to the program as the standard says. `gl_draw` reads the vertex attributes from the JavaScript array buffers, runs the vertex shader once per vertex, clips against the near plane, and rasterizes triangles, lines and points with perspective-correct varyings, the depth test, blending, culling, scissor, color mask and polygon offset. The JavaScript side (`WebGLRenderingContext` in `web/prelude.js`) keeps buffers, textures, framebuffers and renderbuffers as typed arrays, converts texture formats to RGBA, and presents the drawing buffer to the canvas once per task through `N.glPresent`.

CSS 3D. Transform functions are kept as `tfn` entries with 16 values; `tf_matrix4` in `web/cssstyle.c` multiplies them into a 4x4 matrix (`rotateX`, `rotateY`, `rotate3d`, `perspective()`, `translateZ`, `matrix3d` included) and `tf_matrix` gives the 2D part for layout. In `web/paint.c`, `k_m4` adds the parent's `perspective` around `perspective-origin`, and `paint_transformed` turns the matrix into a 3x3 homography of the element's plane: when it is affine the old path is used, otherwise each screen pixel is mapped back through the inverse homography. `bf_hidden` multiplies the orientation of the element and of its `preserve-3d` ancestors and skips elements whose back faces the viewer when `backface-visibility: hidden`. Transitions and animations interpolate the new functions value by value and fall back to a 4x4 matrix when the lists differ.

Unresponsive pages. While a page job runs longer than 8 s, `web_paint` in `apps/internet/browser.c` shows the "Page unresponsive" box; a click on Stop page sets `g_busy_esc` (the same as Esc), Wait hides it for 15 s.

Inert inserted scripts. `frag_scripts_inert` in `web/js.c` sets `NF_SCRIPTDONE` on every `script` element of a fragment parsed for `innerHTML` (except into a `template`), `outerHTML` and `insertAdjacentHTML`, so `on_ins` in `web/engine.c` does not run them. `createContextualFragment` goes through a `template`, so its scripts run when the fragment is inserted, as the standard says.

## 13.59 Ports between frames, reliable parallel requests, WebGL 2 (v10.36)

Ports between frames. Frame messages are JSON strings carried by `N.framePost` and `N.frameRecv`. When `postMessage` on a frame proxy has ports in its transfer list, `xpOut` gives each port an id, marks it with `_rel` (relay: the id and the frame route) and keeps it in the `XP` table; the payload carries the ids. On the other side `xpIn` makes a new `MessagePort` per id with `_rp` (remote: the id and the route back). `MessagePort.postMessage` forwards to the route when the port is a remote one, or when its partner is a relay; the payload `{k: 'p', i: id, d: data, p: ids}` is handled in `framePoll` by `xpRecv`, which delivers to the remote port or to the partner of the relay. The route is `FRAME_PARENT` in a frame and the frame's `contentWindow` proxy in the parent (`FrameWindowProxy._send`).

Connections and the handshake. `pc_get` keeps `busy` set on a new connection until the request gives it back with `pc_put` (before, it was cleared right after the TCP connect, and with background requests another fiber could pick the connection during the TLS handshake). `tls_connect_sess` keeps the parsed ServerHello on the stack instead of in a static variable. `dns_try` waits by time (`g_ticks`, 150 ticks, the question sent again after 50 and 100) instead of by the number of received frames; `dns_query` tries the servers twice. `nwk_net_keep` marks a `keepalive` request; `nwk_net_cancel_owner` leaves such a request running as an orphan, and `fetch` with `keepalive` starts the request at once instead of in a timer.

Switches. `g_web_netbg` (`nwk_net_start` returns 0, so `fetch` and XHR use the old one-at-a-time path), `g_web_csp` (`csp_allow` logs "[CSP off, allowed]" and allows; `frame_refused` returns 0) and `g_ck_samesite` (`cookie_header2` skips the SameSite filter), set by the Terminal command `browser` and saved as `broff=` in `/etc/nvx-setup-done`.

WebGL 2. `WebGL2RenderingContext` extends the WebGL 1 class in `web/prelude.js`. `glsl300` rewrites a `#version 300 es` shader into the GLSL ES 1.00 dialect the compiler in `web/webgl.c` reads: `in` and `out` become `attribute` and `varying`, `layout(location)` on vertex inputs becomes attribute bindings at link time, the first fragment output becomes `gl_FragColor`, and a uniform block becomes a struct uniform named after the block (or plain uniforms when it has no instance name). `std140` computes the member offsets; before each draw `_ublocks` copies the members out of the bound uniform buffer into the program's uniforms when the buffer changed. The compiler adds `textureLod`, `textureGrad`, `textureProj`, `texelFetch` and `textureSize` (`I_TEX` with `aux` 2 and 3), `round`, `trunc`, and maps `uint` and `uvec` to the integer types and the extra sampler types to `sampler2D` or `samplerCube`. Instanced drawing reuses the per-instance path of `ANGLE_instanced_arrays`; sampler objects override the texture parameters during a draw; `blitFramebuffer` copies color between the read and the draw framebuffer with nearest scaling. 3D and array textures, transform feedback and more than one color attachment are accepted but do nothing.

## 13.60 Visible frames first, a page allocator that reuses memory (v10.37)

`frameScan` sorts frame candidates: visible frames (`frVis`: at least 24x24 pixels, on the screen, not `display: none`, `visibility: hidden` or `opacity: 0` on the frame or an ancestor) first, then frames with waiting messages, then the rest. A visible frame takes a free place, else the place of a running frame that is not visible (loaded, or opening for more than 3 s), else the oldest quiet place; a frame with waiting messages does not take the place of a visible frame.

`web/krt.c`: large blocks (over 64 KB) live in `lfree`. `malloc` takes the first free block that is large enough and splits off a remainder of 64 KB or more. When the bump area is full, `lf_coalesce` sorts the free list by address (merge sort), joins neighbours and gives a free block at the top back to the bump pointer, and the request is tried again; a small request that still fails is cut from a free large block (`carve`). `realloc` grows the block at the top of the bump area in place.

## 13.61 Code for self-reported errors, frame reasons (v10.38)

The report watcher in the prelude (the one that writes "Page reported its own error") passes the reported stack to `N.errSnippet`, or builds `(script:line:column)` from the reported fields; `n_errsnippet` in `web/js.c` runs `js_err_snippet`, which finds the script text in the engine's source list (`eng_src_get`) and logs the "Code at" line.

Each frame place (`fslot`) keeps `why`, set when the frame request fails (by the error code) or when `frame_refused` refuses it (`g_frame_refuse_why`); failures are also kept by address in `g_ffail` (8 entries). `nwk_frame_status` returns the place's state (1 loading, 2 running, 3 failed) and reason, or 3 with the remembered reason, or 0 when no place runs that address. `paint.c` draws the reason and the address in the grey box. In the prelude, `frFailed` remembers failed frames by key so `frameScan` does not open them again, and a visible frame may take the place of a failed one.

Visibility cache. `frVis` in the prelude keeps each frame's answer in `frVisC` (a `WeakMap`) for 300 ms and only then calls `frVis0`, the walk over the frame and its parents with `getComputedStyle`. `frameScan` and `mediaScan` keep the result of `querySelectorAll` in `frAll` and `msAll` and search again only when `N.domGen()` (the `g_dom_gen` counter that `dom.c` increments on every insert, remove and attribute change) differs from the value they stored.

## 13.62 Every frame failure keeps its reason, retry when shown (v10.39)

`frame_fail_why` in `apps/internet/webglue.c` stores the reason in the frame place, remembers it by address in `g_ffail` and writes "Embedded page failed" to the error log; it is used for network failures and when the frame's page engine call returns -1. `nwk_frame_fail` (native `N.frameFail`) lets the prelude record its own reason for the selected place, and `nwk_frame_status` reports 3 whenever a place that is not running has a reason. In the prelude the load limit is 60 s; `frFailed` holds `{k, n, v}` (key, failures, visible when it failed) and `frBlocked` allows a new attempt when a frame that failed while hidden is now visible and has failed fewer than 3 times.

## 13.63 Four frames, real pop-up windows, keys in frames, CSS and stability (v10.41)

Frames. `build.sh` now makes five copies of the page engine (`nwe2_entry` ... `nwe6_entry`, one loop, each copy with its own globals): slots 0-3 are for frames (`NFS_IF`), slot 4 (`PSLOT`) is the pop-up window. `webglue.c` keeps a table of entries and fail functions (`g_fent`, `g_ffn`), and the per-copy names `nwk_frame_post_upK`/`nwk_frame_recv_downK`. The frame scan in `prelude.js` uses `FR.S` with four slots. A click in a frame now also gives it the keyboard (`g_kfocus`, `frame_key`), and the wheel scrolls the frame under the mouse (`frame_wheel`). Forms in frames and pop-ups can post (`frame_take_nav` keeps the method and body).

Pop-up windows. `window.open(url, name, features)` with features or a name opens a pop-up when the page has a transient user activation (a trusted click or key in the last 5 s, `G.__actT`), like Chrome; without one it returns `null` and logs "Pop-up blocked". Opening uses up the activation. `noopener`/`noreferrer` open the window but return `null`. The returned `PopupProxy` has `postMessage` (both ways, origin checked), `closed`, `close()`, `focus()`, `name` and a `location` that can be set; reading `document` throws the cross-origin `SecurityError`. In the pop-up `window.opener` exists and `window.close()` closes it. A pop-up opened by a frame (the "Sign in with Google" button is a frame) is relayed through the page: the frame sends `{k:'po'}`, messages go `pm`/`pf`, closing `px`/`pc`. The browser draws the pop-up as a window over the tab (`popup_draw` in `browser.c`: address with Secure/Not secure, a close button), clicks and keys go to it, X-Frame-Options does not apply to it.

CSS. `round()`, `mod()`, `rem()`, `abs()`, `sign()`, `sin()`, `cos()`, `tan()`, `asin()`, `acos()`, `atan()`, `atan2()`, `pow()`, `sqrt()`, `hypot()`, `log()`, `exp()`, angle units and `pi`/`e` in `calc()`; `:has()` with `>`, `+` and `~`; `:nth-child(An+B of S)`; cascade layers (`@layer` blocks and statements, nested names, anonymous layers, layer order before specificity, reversed for `!important`; kept for cached style sheets); `@property` initial values and `CSS.registerProperty`; `filter` (blur, brightness, contrast, grayscale, invert, opacity, saturate, sepia, hue-rotate; drawn after the element by comparing with the pixels before it; drop-shadow is read but not drawn); `display: flow-root` and container query containers start a new block formatting context (margins no longer collapse through them); `getComputedStyle` falls back to the winning declared value (with `em`/`rem` turned into px) for properties it has no special code for; alpha in colours is printed like Chrome (0.5, 0.267); `CSS.supports` checks the property name and the brackets, `and`/`or`/`not` and `selector()` (it no longer answers true for everything). `getBoundingClientRect` includes rotate and scale of the element and its parents.

HTML. `valueAsNumber` for date, month, week, time and datetime-local; `meter` and `progress` values as numbers and `progress.position`; `output.value`/`defaultValue`; `ol.reversed`, `ol.start`, `ol.type`.

Stability of the script engine (no change to the interpreter loop). An out of memory error can now be caught by the page (QuickJS-ng made every InternalError uncatchable; only the time limit stays uncatchable). After an out of memory error, and whenever more than 70 % of the script memory is in use (at most every 2 s), the next tick runs the garbage collector and `about:errors` says how much memory is left. The automatic collection threshold never goes above 7/8 of the limit, so the collector runs before the limit is reached. Test page `p41/heavy.html`: a 3.3 MB script with 40000 functions, out of memory caught and recovered, 200000 objects, 20000 cycles, a 26 MB string and a chain of 50000 promises in 3.2 s on the host.

## 13.64 Web Workers on the second CPU core (v10.42)

Before v10.42 a `Worker` ran its script on the page's own thread, inside a `with` scope, so a busy worker stopped the page. Now a worker runs on CPU 1 when it can.

The worker engine. `web/wk.c` with its own small prelude `web/wk_prelude.js` (embedded as `nwe_wkprelude`), QuickJS, `web/krt.c` and the maths library are linked into one blob, `build/wkx_all.o`, in which every name except `wkx_entry` and `wkx_fail` is local (`objcopy --keep-global-symbol`), the same way the frame copies of the page engine are made. It has its own QuickJS runtime and its own memory arena, so nothing in it is shared with the page engine on CPU 0. All workers of a page share this one runtime as separate `JSContext`s (at most 128), which is enough because there is one spare core.

The kernel side (`apps/internet/webwork.c`). `nwk_wk_ok` checks that the other cores run (`g_smp_on`, CPU 1 online) and on first use allocates two 8 MiB rings (page to worker and worker to page, one writer and one reader each, so no lock is needed: the writer moves `head`, the reader `tail`, with memory barriers between data and index), a 4 MiB stack with a guard page and the arena (256 MiB, or the largest of 192, 128, 96, 64, 48 or 32 MiB that `nwk_alloc_arena` allows; the QEMU tests with 2 GiB of memory got 32 MiB). `ap_loop` on CPU 1 calls `wk_ap_turn` after each scheduling round: when there is a message or a worker timer is due, it switches to the worker stack (`nwe_call_stack`) and calls `wkx_entry`, which handles up to 25 ms of messages, runs the due timers and the promise jobs of every worker, and returns how long until the next timer. A message from the page wakes CPU 1 with `smp_kick(1)`. Linux programs scheduled on CPU 1 still get their turns between worker turns. A CPU exception inside the worker engine is caught by `wk_fault_redirect` (called before `web_fault_redirect` in the exception handler): it jumps to `wkx_fail`, every worker gets an error event and the engine starts fresh on the next message; after 32 such faults the second-core workers stay off until restart.

Messages. Values are copied with QuickJS's own serializer (`JS_WriteObject`/`JS_ReadObject` with object references), so objects, arrays, cycles, `Map`, `Set`, `Date`, `RegExp`, typed arrays and `ArrayBuffer` arrive intact; the page side calls `structuredClone` first, so functions and DOM nodes throw `DataCloneError` as in Chrome. Message kinds: start (script text, address, name, user agent), message, terminate, reply, terminate-all (sent by `js_init` of the main page, so leaving a page stops its workers); back to the page: message, error (text and stack, which becomes an `ErrorEvent` with file, line and column), console line, request and closed. A message is at most 6 MiB.

What a worker has. `self`, `postMessage`, `onmessage` and the event listener functions, `close()`, `setTimeout`/`setInterval` (repeating timers at least 4 ms), `queueMicrotask`, `console`, `performance.now()`, `TextEncoder`/`TextDecoder` (UTF-8 in C, invalid bytes become U+FFFD or a `TypeError` with `fatal`), `atob`/`btoa`, `structuredClone`, `location`, `navigator` (the page's user agent, `hardwareConcurrency` 2), `fetch` (asked of the page, which runs its own `fetch` and sends status, headers and body back) with `Headers` and `Response`, and `importScripts` (the worker waits up to 30 s while the page loads the script, keeping other messages aside). Errors call `self.onerror` and `error` listeners first, as in Chrome. The interrupt handler stops a callback that runs for more than 20 s, and `terminate()` stops even a busy loop at once (a per-worker flag the interrupt handler checks).

When the page thread is used instead. `prelude.js` looks at the worker's script: if it uses something the worker engine does not have (WebAssembly, IndexedDB, OffscreenCanvas, XMLHttpRequest, Blob or File, URL, WebSocket, MessageChannel, BroadcastChannel, crypto, Intl, streams, AbortController and a few more, or module syntax), if the page is a frame or pop-up, or if the other cores are not running, the worker runs on the page thread as before. `__nvxWorkerCores()` tells a test page how many workers run on the second core.

Host testing. `tools/webhost.c` builds the same blob for Linux and runs `wkx_entry` on its own thread with mutex-protected queues; `WH_WK=0` turns it off. Test pages `p42/wk.html` (messages of every type, errors, timers, fetch, console, close, a 300 ms busy worker while a page timer keeps running), `p42/wk2.html` (2000 messages, a 5 MiB buffer, `terminate()` of an endless loop, 40 workers, intervals, `once` listeners, `self.onerror`) and `p42/e.html` give the same results with and without ASan.

## 13.65 AVIF and JPEG XL images (v10.42)

`web/imgx.c` decodes both formats inside the page engine (the engine may use floating point, the kernel may not); `img_load` in `engine.c` calls `imgx_decode` before `nwk_image`, which still decodes PNG, JPEG, WebP, GIF, BMP and ICO in the kernel. `-1` from `imgx_decode` means "not AVIF or JPEG XL", `-2` a broken file (the image shows as broken).

AVIF. The container is read in `imgx.c`: `ftyp` with the `avif` or `avis` brand, then `meta` with `pitm`, `iinf`/`infe`, `iloc` (versions 0-2, data in the file or in `idat`, several extents), `iprp` (`ipco` properties and `ipma` associations), `iref` (`dimg` for grids, `auxl` for alpha) and the properties `ispe`, `colr` (nclx), `auxC`, `irot` and `imir`. Grid images (`grid` items made of tiles) are put together tile by tile, an alpha item (auxC `urn:mpeg:mpegB:cicp:systems:auxiliary:alpha`) fills the alpha channel, and rotation and mirroring are applied last. The AV1 data goes to dav1d 1.5 (`third_party/dav1d`, BSD licence; C only, no assembly, 8 and 16 bit templates, one thread, `third_party/dav1d/config.h` written by hand instead of meson). YUV to RGB uses the matrix from `colr` or the sequence header (BT.601, BT.709, BT.2020, FCC, SMPTE 240M, identity), limited or full range, 8, 10 and 12 bit, and 4:2:0 and 4:2:2 chroma is upsampled bilinearly like libavif. Compared with Pillow/libavif the pixels differ by at most 2 (p99 1). Animated AVIF shows its still primary item; HDR transfer curves (PQ, HLG) are not tone mapped.

JPEG XL. J40 (`third_party/j40`, a public domain single-file decoder, version 2270) with three fixes made for NoovexOS: an image that fits in one group (one TOC entry) read its sections in the wrong order (HfGlobal before LfGroup), so every small lossy image failed; the LF colour correlation factors used 127 instead of 128 as their zero point, which turned colours pink at encoder effort 7 and above; and the Gaborish smoothing filter was parsed but never applied (now applied per LF group before the colour conversion). Compared with libjxl through FFmpeg: lossless images are exact, lossy images differ by at most 3 in 99 % of pixels (the edge-preserving filter is still not applied). Not supported by J40: modular images with the Squeeze transform (lossy alpha and lossy modular mode), patches, splines, noise, animation, progressive files; these show as broken images. J40's internal checks call `imgx_j40_trap`, which jumps out of the decoder instead of the undefined behaviour J40 has without debug mode.

Both decoders were run over 1400 damaged files (bytes changed, cut short, bytes inserted) under ASan and UBSan without a crash. `Accept` headers now list `image/avif` and `image/jxl`, and `<picture><source type="image/avif">` is chosen. The host harness links dav1d too (`build.sh` in the harness folder).

## 13.66 Smart address bar, bookmarks bar and site permissions (v10.42)

Address bar (`apps/internet/browser.c`). While typing, a list of up to 8 suggestions opens under the bar: open tabs, bookmarks and history entries whose address or title contains the typed text, and a "Search for" line when the text has a space or no dot. Up and Down move through the list, Enter or a click opens the choice, Escape closes it. The bookmarks bar under the address bar shows the bookmarks when there are any (`g_bmbar`).

Site permissions. Six permissions per site: notifications, geolocation, camera, microphone, pop-ups and clipboard reading, each Ask, Allow or Block, saved per account in `permissions.txt` in the browser profile (at most 256 sites). `nwk_perm_get`, `nwk_perm_ask` and `nwk_perm_poll` (natives `permGet`, `permAsk`, `permPoll`) connect the page to the browser: asking shows a bar under the address bar ("site wants to show notifications", Allow, Block, x); one question is shown at a time and a second one waits until the first is answered (`-3` from `nwk_perm_ask`). The lock icon in the address bar opens the site menu with the six settings (a click cycles Ask, Allow, Block), and `about:permissions` lists and clears them. In the prelude `navigator.permissions.query` returns a `PermissionStatus` with `onchange`, `Notification.requestPermission` and `Notification.permission` use the stored value, `navigator.geolocation` asks first (allowed: error 2, there is no location source; blocked: error 1), and a blocked pop-up offers "Always allow" for the site.

## 13.67 Partial layout (v10.42)

A change inside an element whose size cannot depend on its contents no longer lays out the whole page. `lay_boundary` in `web/layout.c` accepts block, flex and grid elements that are not floating or sticky, clip their overflow in both directions, have a fixed height in pixels and a fixed width (or an automatic width in normal flow), and are not `html` or `body`. `eng_lay_mark` (called for text changes, child list changes, attribute changes that are not style, `innerHTML` and shadow hosts) walks up from the changed node to the nearest boundary and records it; `eng_layout_if_dirty` then calls `layout_partial`, which builds the boundary's boxes again with `relayout_one` at the same position and containing block size, checks that the outer size and position came out the same (otherwise the whole page is laid out), and copies the parts of the old box that the parent uses. Up to 32 boundaries per layout and 64 partial layouts in a row are allowed before a full layout. On a page with 3000 paragraphs a change inside a fixed-size box takes 0.47 ms instead of 24.8 ms, with the same rectangles as a full layout; `WH_PERF=1` in the host harness prints the counts and times.
