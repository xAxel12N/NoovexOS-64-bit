/* Derived from OpenBSD sys/dev/pci/if_iwx.c, if_iwxreg.h (rev 1.230 / 1.75). SPDX-License-Identifier: ISC AND BSD-3-Clause */
/*
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
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
#include "drivers/net/wifi/intel/iwx/iwx.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/wifi/wifi_hw.h"
static int iwx_last_rssi=-100;
static struct iwx_softc* iwx_gs(void* c){ struct iwx_softc* sc=(struct iwx_softc*)c; return sc&&sc->state>=IWX_ST_ALIVE?sc:0; }
extern int iwx_rx_dec;
static void iwx_glue_rx(const u8* f,u32 len,int rssi){ iwx_last_rssi=rssi; wifi_hw_rx_dec(f,(int)len,rssi,iwx_rx_dec); }
static int iwx_g_init(void* ctx){ struct iwx_softc* sc=(struct iwx_softc*)ctx; if(!sc||iwx_start()!=0)return -1; iwx_rx_deliver=iwx_glue_rx;
    if(iwx_init_hw_cmds(sc)==0)return 0;
    if(sc->state==IWX_ST_ALIVE){ sc->state=IWX_ST_HWFAIL; if(!sc->err)sc->err="firmware did not accept setup commands"; } return -1; }
static int iwx_g_scan_start(void* ctx,const u8* ch,int n){ (void)ch;(void)n; struct iwx_softc* sc=iwx_gs(ctx); return sc?iwx_scan_start(sc,0,0):-1; }
static int iwx_g_scan_done(void* ctx){ return iwx_gs(ctx)?!iwx_scan_busy():1; }
static int iwx_g_join(void* ctx,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht){ (void)ht; struct iwx_softc* sc=iwx_gs(ctx); extern int g_wpa_akm; if(!iwx6_join_ok(ch,(u8)(akm&2u)))return 2; if(iwx6_is_vch(ch)&&g_wpa_akm!=8){ klog("iwx: 6 GHz needs WPA3-SAE; this join selected WPA2-PSK, not joining"); return 2; } return sc?iwx_join_ssid(sc,bssid,ch,ssid):-1; }
static void iwx_g_leave(void* ctx){ struct iwx_softc* sc=iwx_gs(ctx); if(sc)iwx_leave(sc); }
static int iwx_g_tx(void* ctx,const u8* f,int len){ struct iwx_softc* sc=iwx_gs(ctx); return !sc||len<0?-1:iwx_tx_frame(sc,f,(u32)len); }
int iwx_set_igtk(struct iwx_softc* sc,int idx,const u8* key,int len);
static int iwx_g_set_key(void* ctx,int idx,int type,const u8* key,int len,int pw){ if(type==WIFI_KEY_BIP){ struct iwx_softc* s=iwx_gs(ctx); return s?iwx_set_igtk(s,idx,key,len):-1; } if(type!=WIFI_KEY_CCMP)return -1; struct iwx_softc* sc=iwx_gs(ctx); return sc?iwx_set_key(sc,idx,2,key,len,pw):-1; }
static int iwx_g_rssi(void* ctx){ (void)ctx; return iwx_last_rssi; }
extern void iwx_ba_drain(struct iwx_softc* sc) __attribute__((weak));
static void iwx_g_poll(void* ctx){ struct iwx_softc* sc=iwx_gs(ctx); if(sc){ iwx_rx_poll(sc); { void (*volatile bd)(struct iwx_softc*)=iwx_ba_drain; if(bd)bd(sc); } } }
static int iwx_g_mac(void* ctx,u8* mac){ struct iwx_softc* sc=iwx_gs(ctx); if(!sc)return -1; iwx_hw_mac(sc,mac); return 0; }
static const struct wifi_hw_ops iwx_hw_ops={ .name="iwx", .flags=WIFI_HW_RXDEC|WIFI_HW_SAE, .init=iwx_g_init, .scan_start=iwx_g_scan_start, .scan_done=iwx_g_scan_done, .join=iwx_g_join, .leave=iwx_g_leave, .tx=iwx_g_tx, .set_key=iwx_g_set_key, .get_rssi=iwx_g_rssi, .poll=iwx_g_poll, .get_mac=iwx_g_mac };
int iwx_glue_register(void){ if(!iwx_probe()||(g_iwx.state==IWX_ST_HWFAIL&&!g_iwx.starts))return -1; return wifi_hw_register(&iwx_hw_ops,&g_iwx); }
void iwx_wifi_hw_probe(void){ iwx_glue_register(); }
void iwx_up(char* out,int cap){
    if(cap<2)return;
    out[0]=0;
    if(iwx_glue_register()>=0)iwx_g_init(&g_iwx);
    iwx_status(out,cap); }
