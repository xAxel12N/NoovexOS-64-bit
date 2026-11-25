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
#ifndef NVX_INTELBT_FW_H
#define NVX_INTELBT_FW_H
#define INTELBT_7260 1
#define INTELBT_8260 2
#define INTELBT_9260 3
typedef struct { u32 cnvi_top, cnvr_top, cnvi_bt, cnvr_bt, build_num; u16 dev_rev_id, timestamp; u8 img_type, build_type, secure_boot, otp_lock, api_lock, debug_lock, min_nn, min_cw, min_yy, limited_cce, sbe_type; u8 bdaddr[6]; } intelbt_tlv_t;
typedef int (*intelbt_frag_fn)(u8 type,const u8* d,int n,void* ctx);
int intelbt_match(u16 vid,u16 pid);
int intelbt_parse_tlv(const u8* d,int n,intelbt_tlv_t* v);
int intelbt_identify(const u8* d,int n,intelbt_tlv_t* v);
int intelbt_name_tlv(const intelbt_tlv_t* v,const char* sfx,char* out,int cap);
int intelbt_name_legacy(const u8* ver,u16 devrev,const char* sfx,int fallback,char* out,int cap);
int intelbt_css_check(const u8* fw,int len,u8 hwv,u8 sbe);
int intelbt_send_fw(const u8* fw,int len,u8 hwv,u8 sbe,u32* boot,intelbt_frag_fn f,void* ctx);
int intelbt_setup(char* msg,int cap);
#endif
