# Scout instructions (all 10 scouts)
Read /home/claude/reports/w1/BRIEF.md first (license rule!). HARD 45 min. Read-only for the repo; use WebSearch/WebFetch.
1. First list what NoovexOS already has in your area (docs/ARCHITECTURE.md, docs/PORTING-WORKLIST.md, drivers/, apps/,
   third_party/) so you never propose something we have.
2. Find the best 5 candidates in your area that we could port: project/driver name, URL, EXACT license with proof
   (SPDX line / file header / LICENSE URL — for Linux files check MODULE_LICENSE("Dual BSD/GPL") and the header),
   firmware needed and its redistribution license, approx. size (lines), dependencies, how it maps onto our existing
   code (e.g. our rtw88 port has a bus layer g_rtw_bus ready for USB), effort estimate in agent-hours, and value for the
   owner's hardware (see BRIEF) or common laptops/desktops.
3. Rank them. Mark anything GPLv2-only or unclear as REJECTED with the reason.
Write /home/claude/reports/w1/<name>-report.md. Final message: max 3 lines.
