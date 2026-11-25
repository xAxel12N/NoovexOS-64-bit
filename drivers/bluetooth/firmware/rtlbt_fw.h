/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2013 Adrian Chadd <adrian@freebsd.org>
 * Copyright (c) 2019 Vladimir Kondratyev <wulf@FreeBSD.org>
 * Copyright (c) 2023 Future Crew LLC.
 * Copyright (c) 2026 NoovexOS project (port to NoovexOS 64)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef NVX_RTLBT_FW_H
#define NVX_RTLBT_FW_H
#define RTLBT_FRAG 252
typedef int (*rtlbt_cmd_fn)(u16 op,const u8* p,int n,u8* rp,int rcap);
#define RTLBT_F_SIMPLE 0x01
#define RTLBT_F_CONFIG 0x02
#define RTLBT_F_MSFT 0x04
#define RTLBT_F_EMB 0x08
#define RTLBT_FWDIR "/lib/firmware/rtl_bt/"
#define RTLBT_FWMAX (2u<<20)
typedef struct { u16 lmp, rev; u8 ver, flags; const char* name; const char* fw; const char* cfg; } rtlbt_ic_t;
int rtlbt_usb_listed(u16 vid,u16 pid);
const rtlbt_ic_t* rtlbt_get_ic(u16 lmp,u16 rev,u8 ver);
int rtlbt_fw_type(const u8* fw,int len,u16* lmp);
int rtlbt_parse_v1(const u8* fw,int len,u8 rom,u8* out,int cap);
int rtlbt_parse_v2(const u8* fw,int len,u8 rom,u8 key,u8* out,int cap);
int rtlbt_blob(const char* name,const u8** p);
int rtlbt_setup(rtlbt_cmd_fn cmd,char* msg,int cap);
#endif
