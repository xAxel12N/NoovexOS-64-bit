# sc9: permissive libraries and small apps for daily use

Area: libraries and small apps that make daily use easier. I read the source tree only and checked licences on the web. Nothing in the repo was changed.

## 1. What we already have (do not propose these)

| Wanted item | Status in v11.06 | Proof |
|---|---|---|
| SQLite | Have it. The amalgamation is in `third_party/sqlite` and runs as a musl program. | ARCHITECTURE.md line 14 |
| libjpeg-turbo, libpng, FreeType, expat | Have them as source ports (CMake through `mcc-port`). NetSurf uses them. | ARCHITECTURE.md line 6828 |
| PNG/JPEG/WebP/GIF/BMP/ICO decoding | Native, in the kernel (`nwk_image`). AVIF goes through dav1d and JPEG XL through J40. | lines 13120, 14051 |
| dav1d | `third_party/dav1d`, but only used for AVIF still images. | line 1188 |
| Opus, Vorbis, MP3, AAC, H.264/5, VP8/9 | FFmpeg in `third_party/ffmpeg`. Video Player and `play` use it. | `build.sh` line 158 |
| HarfBuzz, Pango, Cairo, GTK3 | Inside the Linux runtime (copied by `ldd`). | line 7495 |
| Office | LibreOffice Writer and Calc ship in APPS.BIN. Native Writer, Editor and PPTX viewer also exist. | lines 1111, 11821 |
| Browsers | Native engine, NetSurf and Firefox (`apt install firefox-esr`). | |
| Calculator, Calendar, Clock | Native apps, but very basic (see ranks 2 and 4). | `apps/office/calculator.c`, `apps/accessories/small_apps.c` |
| ZIP extraction in Files | `zip_extract` | `apps/system/file_explorer/clicks_and_drops.c:12` |
| gzip, bzip2, xz, zstd tools | Have them. | line 6046 |

These are missing:
- **Any PDF viewing in the native system.** The browser only downloads `.pdf` files (`info_pages.c:120`).
- FLAC and ALAC audio.
- AV1 video.
- Decimal numbers in the calculator.
- Calendar events.
- Notes.
- Spell check.
- An email client.
- 7z and tar extraction in Files.

Constraint: native apps run inside the kernel, and the kernel may not use floating point (only the web engine's own stack may, line 14051). A large C++ library therefore belongs in a ring-3 musl or ports program, with the native window acting as the front end.

## 2. Ranked candidates (daily value per effort)

| # | Item | Source / URL | Exact licence (proof) | Size | Deps | How it fits our code | Effort | Value |
|---|---|---|---|---|---|---|---|---|
| 1 | **FLAC + ALAC (+ more PCM) in FFmpeg, and AV1 video through our dav1d** | already in `third_party/ffmpeg` and `third_party/dav1d` | FFmpeg `libavcodec/flacdec.c` header: "GNU Lesser General Public License ... version 2.1 ... or (at your option) any later version". dav1d is BSD-2-Clause. | Builds files that are already in the tree; no new code | none | `build.sh:158`: add `flac` to `--enable-demuxer`; add `flac,alac,pcm_s24le,pcm_s32le,pcm_f32le` to `--enable-decoder`; add `--enable-libdav1d` (or the native `av1` decoder with dav1d) for AV1 MP4/WebM. Add `.flac` to the Video Player's extension list (line 7229). Later, stop refusing AV1 in `isTypeSupported` (line 2740). | 1-3 h (FLAC under 1 h) | High: music collections, YouTube AV1 |
| 2 | **Calculator with decimals, %, +/-, memory, keyboard input (own code)** | own | n/a | about 250 lines | none | `apps/office/calculator.c` is 37 lines of `i64` math, and the "." button does nothing (`calc_press` ignores it). Needs decimal fixed point (i128 scaled by 10^12) because the kernel has no floating point, plus a history line. | 2-3 h | High: used every day, and right now it is visibly broken |
| 3 | **PDF viewer: Poppler (Splash backend) as a ports program + a native "PDF" window** | https://poppler.freedesktop.org | xpdf-derived core: "Xpdf is licensed under the GNU General Public License (GPL), version 2 or 3" (Poppler README-XPDF), so usable as GPLv3. Poppler's own additions are GPL-2.0-or-later. | about 180k lines of C++ | FreeType, libjpeg-turbo, libpng and zlib, which we already port. Optional: openjpeg (BSD-2) and lcms2 (MIT). No Cairo or Qt needed (Splash). | Build `libpoppler` + `pdftoppm`/`pdftotext` with the same CMake/`mcc-port` flow as FreeType (line 6828). A native window runs `pdftoppm -r DPI -f N -l N` as a job, reads the PPM through a pipe, and blits it like `img_viewer_draw`. Open `.pdf` from Files and from browser downloads. `pdftotext` also gives search. | 8-14 h | High: PDFs are everyday documents (bills, school, forms) |
| 3b | Alternative: pdf.js inside our web engine | https://github.com/mozilla/pdf.js | Apache-2.0 (LICENSE: "Apache License Version 2.0, January 2004") | about 2 MB of minified JS | engine Canvas2D, Promise, typed arrays | `.pdf` opens as a built-in viewer page with the fake-worker mode. Less build work, but probably slow on QuickJS and needs Path2D and putImageData complete. | 6-12 h | Fallback if the Poppler build is blocked |
| 4 | **Calendar events + reminders + .ics import/export (own code)** | own; libical (https://github.com/libical/libical) only as a reference | libical README: "licensed under the Mozilla Public License (MPL) v2.0. Alternately, ... LGPL v2.1". Allowed, but about 70k lines is overkill. | about 500 lines | none | `calendar_draw` in `small_apps.c` only draws the month grid. Add events per day (dots in cells), an add/edit dialog, storage in `~/calendar.ics` (VEVENT subset: DTSTART, DTEND, SUMMARY, RRULE FREQ=WEEKLY/YEARLY), and a toast from the clock tick for reminders. Files opens `.ics` into the Calendar. | 4-6 h | Medium-high |
| 5 | **Spell check in native Writer: Hunspell + sv_SE/en_US dictionaries** | https://github.com/hunspell/hunspell | `hunspell.cxx` header: "Version: MPL 1.1/GPL 2.0/LGPL 2.1 ... either the GNU General Public License Version 2 or later ... or the LGPL 2.1 or later", plus the BSD MySpell notice. Dictionaries: en_US (SCOWL, BSD-style) and sv_SE ("Den stora svenska ordlistan", believed LGPL-3, **verify the README before shipping**). | about 25k lines of C++ | none | Build as a musl library + small `spelld` helper (pipe protocol, like `hunspell -a`). Writer underlines words the helper returns. LibreOffice already ships dictionaries we could reuse. | 6-10 h | Medium (Swedish owner) |
| 6 | Sticky Notes app (own code) | own | n/a | about 300 lines | none | New `W_NOTES`: a coloured note window that saves itself to `~/Notes/*.txt` after each edit and reopens at boot. Reuses the Editor's text widget. | 3 h | Medium |
| 7 | 7z / tar.* / rar extraction in Files: libarchive (`bsdtar`) | https://github.com/libarchive/libarchive | BSD-2-Clause (COPYING) | about 150k lines of C | zlib, bzip2, xz, zstd (all in the tree) | Build `bsdtar` as a musl program. "Extract here" in Files runs it for archives that are not ZIP. | 4-8 h | Medium |
| 8 | Email: turn curl's IMAP/SMTP/POP3 back on now; a GUI client later (libetpan) | curl (in tree); https://github.com/dinhvh/libetpan | curl: curl licence (MIT-like). libetpan COPYRIGHT: BSD-style; its ICU charconv data is Unicode-3.0 (permissive). | curl: a flag change. libetpan: about 100k lines | mbedTLS (in tree) | The curl build excludes POP3, IMAP and SMTP (line 6803). Removing those `--disable` flags is trivial. A native mail window (IMAP list, MIME, HTML through the engine) is XL work. Thunderbird through apt is the cheap path for now. | flag 0.5 h; GUI 20+ h | Medium |

## 3. Rejected / not worth it

- **MuPDF**: REJECTED. It is AGPL-3.0. GPLv3 section 13 does allow combining it, but AGPL is not on the BRIEF's allowed list, and its network clause would apply. Needs the owner's decision; Poppler covers the need.
- **PDFium**: allowed (BSD-3 / Apache-2.0) but REJECTED on effort. It needs gn, abseil and C++20, and is about 1M lines with third_party.
- **HarfBuzz / FreeType inside the native web engine**: little value for Latin scripts. HarfBuzz is already in the GTK runtime. On the question asked: FreeType is dual FTL / GPL-2.0-or-later, so we use it under GPLv2+ and the FTL question does not matter (the FSF lists FTL as GPLv3-compatible anyway).
- **SQLite, libjpeg-turbo, libpng, libwebp, Opus, Vorbis, dav1d**: we already have them (section 1).

## 4. Suggested order

1. FFmpeg flags for FLAC (under 1 h), then AV1 with libdav1d.
2. Calculator decimals.
3. Calendar events + .ics.
4. Poppler PDF viewer.
5. Sticky Notes.
6. Hunspell.
7. libarchive.
8. curl mail protocols.
