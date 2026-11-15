# web

The NoovexOS web engine: HTML parser and DOM, CSS, layout (floats, tables, flexbox, grid), painting, fonts, SVG and the QuickJS bridge.


## Folders (v10.99)

- core/ engine entry, utilities, kernel runtime glue, Rust stub
- html/ DOM and HTML parser
- css/ CSS parser, style resolution, animations
- layout/ block, inline, flex and grid layout
- paint/ painting, canvas 2D, SVG, WebGL
- text/ fonts, TrueType, web fonts, WOFF2
- image/ image decoders
- codec/ Brotli
- js/ JavaScript bindings, WebAssembly, prelude
- worker/ Web Worker engine (second CPU core)
- media/ audio and video elements

Public headers stay here: nwe.h, css.h, layout.h, canvas.h.


