Task iwxj (shared worktree /home/claude/wt/iwxi, branch p-iwxi, where the integrator is wiring iwx into the build right now): finish the
Intel Wi-Fi data path pieces iwx2 left as stubs: join (auth/assoc via MAC context + binding + time event + station add, as OpenBSD
if_iwx.c does), ADD_STA_KEY for CCMP pairwise+group (so WPA2 works via the generic path once the handshake moves), and AX210/AX211 PNVM
loading (iwx_fw.c area: send the .pnvm via the context-info path after ALIVE as OpenBSD does). You own ONLY drivers/net/wifi/intel/iwx/mvm/iwx_cmd.c,
iwx_data.c, iwx_scan.c and a new iwx_pnvm.c; the integrator owns everything else (Makefile/units/includes/glue/shell). Commit only your
files with `git add <files>`; ccheck them. Read /home/claude/reports/p1/iwx*-report.md and hwops-report.md first.
