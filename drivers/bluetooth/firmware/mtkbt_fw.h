/*
 * SPDX-License-Identifier: ISC
 *
 * Copyright (C) 2021 MediaTek Inc.
 * Copyright (c) 2026 NoovexOS project (port to NoovexOS 64)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef NVX_MTKBT_FW_H
#define NVX_MTKBT_FW_H
#define MTKBT_HDR 32
#define MTKBT_GD 64
#define MTKBT_SEC 64
#define MTKBT_SPEC_OFF 12
#define MTKBT_SPEC_LEN 52
#define MTKBT_CHUNK 250
#define MTKBT_MAXSEC 64
#define MTKBT_FWMAX (4u<<20)
enum { MTKBT_WMT_PATCH_DWNLD=0x01, MTKBT_WMT_FUNC_CTRL=0x06, MTKBT_WMT_SEMAPHORE=0x17 };
enum { MTKBT_ST_INVALID, MTKBT_ST_PATCH_UNDONE, MTKBT_ST_PATCH_PROGRESS, MTKBT_ST_PATCH_DONE, MTKBT_ST_ON_UNDONE, MTKBT_ST_ON_DONE, MTKBT_ST_ON_PROGRESS };
typedef struct { u32 off, size, idx; const u8* spec; } mtkbt_sec_t;
int mtkbt_match(u16 vid,u16 pid);
int mtkbt_supported(u32 dev_id);
int mtkbt_fw_name(char* out,int cap,u32 dev_id,u32 fw_ver,u32 flavor);
int mtkbt_parse(const u8* fw,u32 len,u32 dev_id,mtkbt_sec_t* s,int cap);
int mtkbt_wmt_status(u8 op,const u8* e,int n);
int mtkbt_attach(u16 vid,u16 pid);
#endif
