# R2 review of w-c2 (v11.03..w-c2, 2 commits)

## MEDIUM
1. DNS: router SERVFAIL/REFUSED (anc==0) now stops the fallback to 8.8.8.8/1.1.1.1 and is cached 15 s (regression; v11.03 tried the next server). Only NXDOMAIN should end the search.
   net/stack/tcpip.c dns_try:
   - `int anc=rd16(d+6); if(anc==0){ netlog("DNS no answer"); return 2; }`
   + `int anc=rd16(d+6); if(anc==0){ netlog("DNS no answer"); return (rd16(d+2)&0xF)==3?2:0; }`
2. DHCP renew drains the NIC while connected. Once per half-lease, for up to ~1.5 s, dhcp_async_step runs in place of net_idle_poll. It ignores g_net_busy/sock_any_open, which net_idle_poll respects, so it can take frames meant for an open native socket (they only go to net_pass, i.e. lxs). TCP recovers by retransmitting, but downloads stall. Fix at the top of the rx loop in dhcp_async_step:
   + `if(da_ren){ extern int sock_any_open(void); if(g_net_busy||sock_any_open())return 0; }`
   (If the socket owner eats the ACK, the renew times out and keeps the address, then retries 10% of the lease later. Bounded.)

## LOW
3. Chunked-truncation check misses a cut exactly on a chunk boundary: at r>=n the parse gives sz=0, so fin=1. In ap_get's chunked loop, count hex digits:
   `long sz=0; int nd=0; while(r<n&&buf[r]!='\r'){ int v=ap_hexval((char)buf[r]); if(v<0)break; sz=sz*16+v; r++; nd++; } if(!nd)break; ...`
4. The DNS negative cache also stores plain timeouts (no reply at all), so a host that failed while Wi-Fi was coming up stays failed for 15 s. Cache only on r==2: move the dneg store inside `neg:` and `return 0` before the label on the fall-through path.
5. A DHCP NAK (type 6) during renew is ignored, so the client keeps a refused address until lease expiry. Optional: in da_st==3 with da_ren, on dhcp_frame(...,6,...) call dhcp_async_start().
6. Syscall length checks cast u64 rdx/rcx to int (82/83/102/103/95/18/120). A length like 0x1_0000_0010 is checked as 16 bytes. If the handlers use the full 64-bit length, that bypasses the check. Use `(long)` and reject >2^24 (jc_ulen_ok takes int: change it to long). NOT verified: I could not find the handlers' length type in time.
7. pk_bkl_restore: if d>0 but the BKL is no longer held after the longjmp, it silently returns and the caller's later unlock underflows. Probably unreachable; consider re-taking the lock.
8. Spurious IRQ: the logic is correct per the 8259A/OSDev docs (IRQ7: read master ISR bit 7, no EOI if clear; IRQ15: read slave ISR, EOI master only). The kernel is PIC-only (pic_eoi is pure 8259), so LAPIC EOI is not lost. Nit: OCW3 0x0B leaves the PIC in ISR-read mode; add `outb(port,0x0A)` after the inb if anything reads the IRR.

## OK
- DHCP renew cannot block: one packet per tick, about 1.5 s timeout, retry after 10% of the lease, rediscover at expiry. No storm; tick math cannot overflow.
- Wi-Fi wrong password is still caught: 3 consecutive r=6/7 (or one r=10) moves autoc to 2 and notifies. Other errors reset the counter, so a mixed sequence can delay the notice but keeps retrying with backoff. Acceptable.
- apt retry is bounded at 3 tries in both places, and the size-mismatch path frees the buffer.
- Not verified (no time): whether NULL is allowed for case 35/110/120/18 (no NULL exemption, unlike 52), and that changing returns from -1 to SIGSEGV doesn't break existing native apps.
