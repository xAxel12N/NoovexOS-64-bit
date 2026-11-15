# v11.04 — shared brief (lead: Claude). 30-minute round, release target ~16:12 (check `date`).

Owner asked: 22 agents. Search for more bugs and safety failures; brainstorm improvements and code the best ones;
review the code; bug hunters report to the coders. NoovexOS 64 is a from-scratch x86-64 OS "for easy daily use"
(desktop, apps, browser, Wi-Fi, USB, Linux-ABI programs). Repo: /home/claude/src/NoovexOS-64-bit-main (v11.03).
Background: docs/handoff-v11.03.md, docs/handoff-v11.02.md (their "Not fixed" lists), docs/team-reports/v1102 + v1103.
The owner's real machine: wireless combo receivers (062A:4101, 248A:16BB), USB camera 0C45:1915 (code 17), USB mic,
USB stick, USB hub, RTL8821CE Wi-Fi, GTX 1650 desktop. v11.03 (drag-end freeze fixes) is not yet tested by the owner.

TEAM
- Safety searchers S1-S4 (read-only): untrusted-input and memory-safety sweeps in areas nobody reviewed yet.
- Bug hunters B1-B4 (read-only): hunt real bugs in daily-use paths, write a patch proposal per finding for the coders.
- Brainstormers I1-I5 (read-only): ideas. FIRST search what already exists (docs/ARCHITECTURE.md, shell/commands,
  apps/) — the owner hates proposals for features we already have. Only small ideas codeable in ~10 minutes.
- Coders C1-C5 (worktrees /home/claude/wt/c1..c5, branches w-c1..w-c5): C1-C3 fix known safety backlog now,
  C4-C5 code the best ideas + bug-hunter findings (lead assigns).
- Reviewers R1-R4 (read-only): review the coders' commits.

RULES: /home/claude/reports/BUILDERS.md. Never run build.sh or tools/check.py (coders may run
`python3 tools/check.py --only usb` or a single small group if their change is covered — say which).
Compile-check with /home/claude/ccheck.sh <file>. Never `git add` build or tools/out. GPLv3-compatible only.
Reports: /home/claude/reports/v1104/<name>-report.md. FINAL MESSAGE: max 3 lines (the lead reads 22).
