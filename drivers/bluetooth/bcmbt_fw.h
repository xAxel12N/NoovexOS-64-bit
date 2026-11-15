/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bluetooth support for Broadcom devices
 * Copyright (C) 2015  Intel Corporation
 * Copyright (c) 2026 NoovexOS project (port to NoovexOS 64)
 */
#ifndef NVX_BCMBT_FW_H
#define NVX_BCMBT_FW_H
typedef int (*bcmbt_cmd_fn)(u16 op,const u8* p,int n,u8* rp,int rcap);
int bcmbt_usb_vendor(u16 vid);
int bcmbt_match(u16 vid,u16 manuf);
const char* bcmbt_hw_name(u16 subver);
int bcmbt_hcd_check(const u8* d,int len,int* nrec,int* nlaunch);
int bcmbt_fw_names(const char* hw,u16 vid,u16 pid,char names[3][48]);
int bcmbt_setup(bcmbt_cmd_fn cmd,u16 vid,u16 pid,char* msg,int cap);
#endif
