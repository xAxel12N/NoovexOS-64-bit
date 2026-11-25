# File moves (old path -> new path)

The project was reorganized into named modules. Use this table to find a file by an old path from the docs, handoffs or notes.

## Browser app

| Old | New |
|---|---|
| `apps/internet/browser.c` | `apps/internet/browser/browser.c` |
| `apps/internet/browser_parts/01_imgpool_ok.c` | `apps/internet/browser/images.c` |
| `apps/internet/browser_parts/02_show_errors.c` | `apps/internet/browser/info_pages.c` |
| `apps/internet/browser_parts/03_perm_bar_click.c` | `apps/internet/browser/site_permissions.c` |
| `apps/internet/browser_parts/04_g_bmbar_on.c` | `apps/internet/browser/mouse_and_bookmarks.c` |
| `apps/internet/webglue.c` | `apps/internet/browser/glue/webglue.c` |
| `apps/internet/webglue_parts/01_fs_save_bin.c` | `apps/internet/browser/glue/startup.c` |
| `apps/internet/webglue_parts/02_chunk_done.c` | `apps/internet/browser/glue/http.c` |
| `apps/internet/webglue_parts/03_nwk_net_us.c` | `apps/internet/browser/glue/fetch.c` |
| `apps/internet/webglue_parts/04_nf_recv.c` | `apps/internet/browser/glue/fetch_queue.c` |
| `apps/internet/webglue_parts/05_nwk_webcache_tick.c` | `apps/internet/browser/glue/webcache_and_frame_errors.c` |
| `apps/internet/webglue_parts/06_frame_paint_cur.c` | `apps/internet/browser/glue/frames.c` |
| `apps/internet/webwork.c` | `apps/internet/browser/webwork.c` |
| `apps/internet/websock.c` | `apps/internet/browser/websock.c` |

## Web engine

| Old | New |
|---|---|
| `web/engine.c` | `web/core/engine.c` |
| `web/util.c` | `web/core/util.c` |
| `web/krt.c` | `web/core/krt.c` |
| `web/rust_stub.c` | `web/core/rust_stub.c` |
| `web/dom.c` | `web/html/dom.c` |
| `web/cssparse.c` | `web/css/cssparse.c` |
| `web/cssstyle.c` | `web/css/cssstyle.c` |
| `web/anim.c` | `web/css/anim.c` |
| `web/layout.c` | `web/layout/layout.c` |
| `web/layout2.c` | `web/layout/layout2.c` |
| `web/paint.c` | `web/paint/paint.c` |
| `web/canvas.c` | `web/paint/canvas.c` |
| `web/svg.c` | `web/paint/svg.c` |
| `web/webgl.c` | `web/paint/webgl.c` |
| `web/font.c` | `web/text/font.c` |
| `web/fontdata.c` | `web/text/fontdata.c` |
| `web/ttf.c` | `web/text/ttf.c` |
| `web/webfont.c` | `web/text/webfont.c` |
| `web/woff2.c` | `web/text/woff2.c` |
| `web/imgx.c` | `web/image/imgx.c` |
| `web/brotli.c` | `web/codec/brotli.c` |
| `web/brotli_tab.c` | `web/codec/brotli_tab.c` |
| `web/js.c` | `web/js/js.c` |
| `web/wasm.c` | `web/js/wasm.c` |
| `web/prelude.js` | `web/js/prelude.js` |
| `web/prelude_gen.c` | `web/js/prelude_gen.c` |
| `web/wk.c` | `web/worker/wk.c` |
| `web/wk_prelude.js` | `web/worker/wk_prelude.js` |
| `web/wk_prelude_gen.c` | `web/worker/wk_prelude_gen.c` |
| `web/wmedia.c` | `web/media/wmedia.c` |

## Wi-Fi driver (rtw88 family)

| Old | New |
|---|---|
| `drivers/net/wifi/rtw8821ce.c` | `drivers/net/wifi/realtek/rtw88/rtw8821ce.c` |
| `drivers/net/wifi/rtw_sec.c` | `drivers/net/wifi/realtek/rtw88/rtw_sec.c` |
| `drivers/net/wifi/rtw_efuse.c` | `drivers/net/wifi/realtek/rtw88/rtw_efuse.c` |
| `drivers/net/wifi/rtw_tables.h` | `drivers/net/wifi/realtek/rtw88/rtw_tables.h` |
| `drivers/net/wifi/rtw_tables_cond.h` | `drivers/net/wifi/realtek/rtw88/rtw_tables_cond.h` |

## Split files renamed into named modules

| Old | New |
|---|---|
| `shell/commands_parts/01_sh_redraw.c` | `shell/commands/prompt_and_dispatch.c` |
| `shell/commands_parts/02_battery_cmd.c` | `shell/commands/system_disks_packages_gpu.c` |
| `shell/commands_parts/03_cmd_nvxlogin.c` | `shell/commands/accounts_files_info.c` |
| `shell/commands_parts/04_cmd_ring3.c` | `shell/commands/processes_devices_wifi_bringup.c` |
| `shell/commands_parts/05_cmd_net.c` | `shell/commands/network_and_apps.c` |
| `shell/commands_parts/06_term_key.c` | `shell/commands/terminal_keys_and_render.c` |
| `graphics/ui/desktop_parts/01_xapps_count.c` | `graphics/ui/desktop/app_tiles_and_icons.c` |
| `graphics/ui/desktop_parts/02_win_title_icon.c` | `graphics/ui/desktop/window_titles_and_bars.c` |
| `graphics/ui/desktop_parts/03_prop_click.c` | `graphics/ui/desktop/desktop_clicks.c` |
| `graphics/ui/desktop_parts/04_desk_win_exposed.c` | `graphics/ui/desktop/present_and_pump.c` |
| `apps/system/settings_parts/01_nvx_settings_save.c` | `apps/system/settings/storage_and_layout.c` |
| `apps/system/settings_parts/02_settings_wheel.c` | `apps/system/settings/pages.c` |
| `sys/linux/linux_parts/01_lx_ok.c` | `sys/linux/syscalls/fifos_and_names.c` |
| `sys/linux/linux_parts/02_ivl_muldivk.c` | `sys/linux/syscalls/timers_and_handles.c` |
| `sys/linux/linux_parts/03_ino_ev_fd.c` | `sys/linux/syscalls/inotify.c` |
| `sys/linux/linux_parts/04_lx_wait_deadline.c` | `sys/linux/syscalls/poll_epoll.c` |
| `sys/linux/linux_parts/05_lx_mmap.c` | `sys/linux/syscalls/memory_and_signals.c` |
| `sys/linux/linux_parts/06_lx_mkdir.c` | `sys/linux/syscalls/files_dirs.c` |
| `sys/linux/linux_parts/07_lx_dup3.c` | `sys/linux/syscalls/fds_and_system_info.c` |
| `sys/linux/linux_parts/08_sysfs.c` | `sys/linux/syscalls/sysfs.c` |
| `sys/linux/linux_parts/09_lx_more.c` | `sys/linux/syscalls/more_syscalls.c` |
| `sys/linux/linux_parts/10_lx_ns.c` | `sys/linux/syscalls/namespaces_containers.c` |
| `sys/linux/linux_parts/11_lx_procx.c` | `sys/linux/syscalls/proc_extra.c` |
| `drivers/usb/xhci_parts/01_xhc_mem_for.c` | `drivers/usb/xhci/controller_init.c` |
| `drivers/usb/xhci_parts/02_hid2ascii.c` | `drivers/usb/xhci/hid_keyboard_mouse.c` |
| `drivers/usb/xhci_parts/03_ufat_poke.c` | `drivers/usb/xhci/storage_fat_and_hubs.c` |
| `drivers/usb/xhci_parts/04_uvc_er_init.c` | `drivers/usb/xhci/uvc_webcam.c` |
| `drivers/usb/xhci_parts/05_usbnet.c` | `drivers/usb/xhci/usb_network.c` |
| `drivers/video/nvidia/nvgsp_parts/01_xzk_decode_into.c` | `drivers/video/nvidia/gsp/firmware_unpack_and_memory.c` |
| `drivers/video/nvidia/nvgsp_parts/02_boot_gsp.c` | `drivers/video/nvidia/gsp/boot.c` |
| `drivers/video/nvidia/nvgsp_parts/03_nvgsp_fb_present.c` | `drivers/video/nvidia/gsp/framebuffer_present.c` |
| `drivers/video/nvidia/nvgsp_parts/04_nvgsp_tick.c` | `drivers/video/nvidia/gsp/tick_on_off.c` |
| `drivers/video/nvidia/nvgsp_parts/05_nvdisp_mode.c` | `drivers/video/nvidia/gsp/display_modes.c` |
| `drivers/video/nvidia/nvgsp_parts/06_nv3d.c` | `drivers/video/nvidia/gsp/accel_3d.c` |
| `apps/system/file_explorer_parts/01_fx_trash.c` | `apps/system/file_explorer/trash_and_sort.c` |
| `apps/system/file_explorer_parts/02_tpc_open.c` | `apps/system/file_explorer/open_and_draw.c` |
| `apps/system/file_explorer_parts/03_fx_rowat.c` | `apps/system/file_explorer/clicks_and_drops.c` |
| `core/init/installer_parts/01_dm_crc32.c` | `core/init/installer/disk_manager.c` |
| `core/init/installer_parts/02_ip_mem_verify.c` | `core/init/installer/install_files.c` |
| `core/init/installer_parts/03_ip_pick_disk.c` | `core/init/installer/disk_picker.c` |
| `core/init/installer_parts/04_inst_svc.c` | `core/init/installer/usb_clone_service.c` |

## apt and process table modules

| Old | New |
|---|---|
| `fs/pkg/apt_parts/01_unp_auto.c` | `fs/pkg/apt/unpack_and_prompts.c` |
| `fs/pkg/apt_parts/02_ar_member.c` | `fs/pkg/apt/deb_archive.c` |
| `core/proc/jobs_parts/01_jc_resume.c` | `core/proc/jobs/resume_and_fds.c` |
| `core/proc/jobs_parts/02_jc_tg_sync_ids.c` | `core/proc/jobs/thread_groups_and_exit.c` |
| `core/proc/jobs_parts/03_segv_str.c` | `core/proc/jobs/faults.c` |

## v11.11

- `drivers/net/wifi/rtw88` -> `drivers/net/wifi/realtek/rtw88`
- `drivers/net/wifi/iwx/iwx_pcie.c` -> `drivers/net/wifi/intel/iwx/trans/iwx_pcie.c`
- `drivers/net/wifi/iwx/iwx_fw.c` -> `drivers/net/wifi/intel/iwx/fw/iwx_fw.c`
- `drivers/net/wifi/iwx/iwx_pnvm.c` -> `drivers/net/wifi/intel/iwx/fw/iwx_pnvm.c`
- `drivers/net/wifi/iwx/iwx_cmd.c` -> `drivers/net/wifi/intel/iwx/mvm/iwx_cmd.c`
- `drivers/net/wifi/iwx/iwx_scan.c` -> `drivers/net/wifi/intel/iwx/mvm/iwx_scan.c`
- `drivers/net/wifi/iwx/iwx_data.c` -> `drivers/net/wifi/intel/iwx/mvm/iwx_data.c`
- `drivers/net/wifi/iwx/iwx_glue.c` -> `drivers/net/wifi/intel/iwx/iwx_glue.c`
- `drivers/net/wifi/iwx/iwx.h` -> `drivers/net/wifi/intel/iwx/iwx.h`
- `drivers/net/wifi/iwx/iwx_cmd.h` -> `drivers/net/wifi/intel/iwx/iwx_cmd.h`
- `drivers/net/wifi/iwx/NOTICE` -> `drivers/net/wifi/intel/iwx/NOTICE`
- `drivers/bluetooth/btusb.c` -> `drivers/bluetooth/transport/btusb.c`
- `drivers/bluetooth/rtlbt_fw.c` -> `drivers/bluetooth/firmware/rtlbt_fw.c`
- `drivers/bluetooth/rtlbt_fw.h` -> `drivers/bluetooth/firmware/rtlbt_fw.h`
- `drivers/bluetooth/intelbt_fw.c` -> `drivers/bluetooth/firmware/intelbt_fw.c`
- `drivers/bluetooth/intelbt_fw.h` -> `drivers/bluetooth/firmware/intelbt_fw.h`
- `drivers/bluetooth/mtkbt_fw.c` -> `drivers/bluetooth/firmware/mtkbt_fw.c`
- `drivers/bluetooth/mtkbt_fw.h` -> `drivers/bluetooth/firmware/mtkbt_fw.h`
- `drivers/bluetooth/bcmbt_fw.c` -> `drivers/bluetooth/firmware/bcmbt_fw.c`
- `drivers/bluetooth/bcmbt_fw.h` -> `drivers/bluetooth/firmware/bcmbt_fw.h`
- `drivers/bluetooth/hci.c` -> `net/bluetooth/hci.c`
- `drivers/bluetooth/hci_parse.h` -> `net/bluetooth/hci_parse.h`
