# Bluetooth host stack

- `hci.c`: HCI core (commands, events, scan, adapter state). Talks to `drivers/bluetooth/transport/`.
- `profiles/`: profiles on top of L2CAP (HID host and others).

## Host contract (`bt_host.h`)

- `hci.c`: `bt_hci_cmd` sends a command and waits for Command Complete/Status (returns the return-parameter length, status in `rp[0]`; for Command Status returns 1 with the status, -1 on timeout). `bt_hci_send` sends without waiting. `bt_hci_wait_evt(code,sub,...)` waits for one event (`sub` = LE subevent for 0x3E, else -1) while still dispatching the rest. `bt_poll()` pumps events and ACL data once (non-blocking); the manager calls it from the main loop. Every event goes to `bt_host_event` (conn.c), which handles link events and forwards everything to `bt_sec_event` (sec.c). Event handlers must not block: no `bt_hci_cmd` inside a handler, use `bt_hci_send`.
- `conn.c`: `bt_connect` (BR/EDR Create Connection or LE Create Connection, blocking with a timeout), `bt_disconnect`, handle lookup, ACL fragmentation/reassembly, controller buffer credits via Number of Completed Packets. `bt_conn_listen` registers up/down callbacks (manager, profiles). Incoming BR/EDR connection requests are accepted when `bt_mgr_accept` (bt_mgr.c) returns 1. `btconn_t.sec`/`mgr`/`att` are per-owner private pointers.
- `l2cap.c`: BR/EDR signalling (connect/config/disconnect/echo/info), `l2cap_connect` (outgoing, blocking with timeout, finishes configuration), `l2cap_server(psm, accept)` for incoming channels (HID devices reconnect on 0x11/0x13), `l2cap_send` (basic mode), `l2cap_close`. LE fixed channels: `l2cap_fixed(BT_CID_ATT/BT_CID_SMP, rx)` and `l2cap_fixed_send`; LE signalling answers connection parameter update requests.
- `sec.c`/`smp.c` provide `bt_sec_event`, `bt_sec_auth` (BR/EDR authenticate + encrypt) and `bt_smp_pair` (LE pair/encrypt); conn.c has weak fallbacks so the tree links without them.
