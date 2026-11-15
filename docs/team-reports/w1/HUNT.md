# Bug hunter / code searcher instructions
Read /home/claude/reports/w1/BRIEF.md first. Read-only. HARD 50 min.
Find REAL bugs (not style): crashes, hangs, data loss, security holes, things the owner would notice.
For each finding: file:line, how it fails (a concrete scenario), severity HIGH/MEDIUM/LOW, confidence %, and an
EXACT small patch the coders can apply (old line → new line). Skip anything already listed as fixed in
docs/handoff-v11.0*.md. Write /home/claude/reports/w1/<name>-report.md (most important first). Final message: max 3 lines.
