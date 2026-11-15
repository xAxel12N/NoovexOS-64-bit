# C2 report (v11.04) — branch w-c2

## M1 native INT 0x80 raw user pointers — core/proc/jobs/thread_groups_and_exit.c
Added to jc_syscall's first (pre-validation) switch; a bad pointer now kills the job with status 11 (jc_finish(g_jcur,11); jc_leave(), same as the sigreturn path) instead of the kernel touching it:
- strings via jc_ustr: 30 open, 19 chdir, 21 mkdir, 38 unlink, 105 resolve (rbx); 121 setenv, 160 link (rbx+rcx); 44/93 text (rdx); 120 getenv name (rbx)
- writes via jc_umapped_w: 35 readdir (rcx,64); 52 getmouse (4 bytes per non-NULL ptr); 83/103 recv (rcx,cap); 95 wm_poll (rbx, cap or 64); 120 getenv out (rcx, cap or 1, since cap<=0 still writes the NUL); 110 stat (path + rcx,12); 18 getcwd (rbx, n or 1)
- reads via jc_umapped: 82 msg_send, 102 sock_send (rcx,len)
- New helpers: jc_ulen_ok(a,n,w) (n<=0 ok, n>16 MB rejected) and macro jc_ufault().
Extra beyond the report list: 18/19/21/38/105/110/160 (same bug class, same fall-through to syscall_dispatch).
None of these numbers is handled in jc_syscall's second switch, so all fell through to syscall_dispatch before.

## M2 preempt-demo BKL depth leak — core/proc/sched.c
Fixed at the k_setjmp sites rather than at interrupts.c:71, because the exit syscall path (sys/native/syscall.c case 0, from isr128_c) also k_longjmps out with the BKL held. preempt_run, preempt_run_elf and compositor_run now save the depth before k_setjmp (pk_bkl_save) and restore it after (pk_bkl_restore: set depth back, or fully release if it was not held before).

## L1 spurious IRQ7/IRQ15 — arch/x86_64/interrupts/interrupts.c isr_body
Before pic_eoi for irq 7/15, read the PIC ISR (OCW3 0x0B). If the bit is clear it is spurious: IRQ7 gets no EOI, IRQ15 gets a master-only EOI, and the handler returns.

## Checks
ccheck: interrupts.c, sched.c, core/proc/jobs.c (includes thread_groups_and_exit.c), sys/native/syscall.c — no new warnings/errors (only pre-existing misleading-indentation warnings).
python3 tools/check.py --only linux: 8 of 8 passed (musl + legacy programs + 3 fuzz seeds).
Not tested in QEMU for the preempt/compositor demos or a real spurious IRQ.

# Round 2 (B3 HIGH items) — commit on w-c2
1. net/wifi/wifi_mgr.c: wm_pwbad[] counter. Auto-join is turned off ("password not accepted") only after 3 wrong-password/sign-in results (6/7) in a row; 10 (certificate changed) still turns it off at once. The counter resets on connect, new slot, and forget-shift. Normal back-off is unchanged.
2. net/stack/tcpip.c: option 51 is parsed (dp_lease -> g_dhcp_lease, g_dhcp_got set on both sync and async ACK). New dhcp_lease_tick(), called each Wi-Fi connected tick: at T1 (half the lease) it sends a non-blocking REQUEST for the current IP via the async machine (da_ren). An ACK refreshes the lease silently (returns 0, so there is no portal re-probe or status change). No answer keeps the address and retries after 10% of the lease. At expiry it runs a full dhcp_async_start(). Infinite leases (0xFFFFFFFF) are skipped. NAK is not parsed: a NAK times out as no answer and the address stays until expiry. Wired (dhcp_config) records the lease but is NOT renewed yet (it would block).
3. fs/pkg/apt/unpack_and_prompts.c ap_get: parses Content-Length; chunked transfers need the 0-size final chunk. A short body returns 0 with g_fetch_err "Connection dropped during download". deb_archive.c: apt_update and the .deb fetch retry up to 3 times on dropped or timed-out connections, and the .deb fetch also retries on a size mismatch. Untested whether https_fetch_to returns full headers incl. Content-Length (it is parsed if present).
4. tcpip.c dns_query: 15 s negative cache per host. NXDOMAIN (anc==0) stops at once (dns_try returns 2 internally). Round 2 is skipped if no server replied in round 1. 10.0.2.3 is only tried when we are on 10.0.2.x (QEMU).
Checks: ccheck tcpip.c, wifi_mgr.c, fs/pkg/apt.c — no errors and no new warnings. No QEMU/network test run.
