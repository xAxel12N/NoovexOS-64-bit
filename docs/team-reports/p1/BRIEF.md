Port round p1 for NoovexOS 64 (GPLv3, from-scratch x86-64 OS). Lead = the main session. HARD LIMIT 45 minutes.
Read /home/claude/reports/BUILDERS.md (rules: no build.sh, no full check.py; compile-check with /home/claude/ccheck.sh <file>;
`python3 tools/check.py --only usb` / `--only bt` / `--only wifi` allowed). Work ONLY in your own worktree /home/claude/wt/<name>,
commit on its branch p-<name> with trailer:
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Qrp2TLw8H5AQsCSKNWWESH
Rules: code style of the tree (dense C, NO comments in new code except licence headers). Licences: only BSD/ISC/MIT/Apache-2.0/
GPL-2.0-or-later code may be copied; keep the original copyright+licence header on any derived file and add an entry to the nearest
NOTICE file. GPL-2.0-only = facts only. Verify the SPDX line by fetching the real file before copying (WebFetch raw.githubusercontent.com).
Owner hardware: RTL8821CE Wi-Fi (works today, MUST NOT BREAK), Realtek BT 0BDA:C123 (works, must not break). The owner has NO Intel/
MediaTek/Broadcom device, so new drivers must be completely inert unless their PCI/USB ID matches, and must fail safely (no hang, every
wait has a timeout, no box spam). Firmware: never commit a blob unless its licence clearly allows redistribution; otherwise load it from
/lib/firmware/... on disk if present and print a clear message naming the missing file. Add a host test under tools/tests if practical.
Write /home/claude/reports/p1/<name>-report.md (what you did, files, what is untested, follow-ups). Final message max 3 lines.
