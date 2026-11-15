/*
 * Derived from OpenBSD sys/dev/pci/if_iwx.c, if_iwxreg.h (rev 1.230 / 1.75).
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
 *
 * Portions: Copyright(c) 2017 Intel Deutschland GmbH, Copyright(c) 2018 - 2019
 * Intel Corporation, used under the BSD-3-Clause branch of the dual BSD/GPLv2
 * licence: Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. Neither the name Intel Corporation nor the names of
 * its contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS PROVIDED
 * BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DAMAGES ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "drivers/net/wifi/iwx/iwx.h"
#include "include/nvx/net/wifi/wifi_hw.h"
static int iwx_last_rssi=-100;
static void iwx_glue_rx(const u8* f,u32 len,int rssi){ iwx_last_rssi=rssi; wifi_hw_rx(f,(int)len,rssi); }
static int iwx_g_init(void* ctx){ struct iwx_softc* sc=(struct iwx_softc*)ctx; if(!sc)return -1; iwx_rx_deliver=iwx_glue_rx; return iwx_init_hw_cmds(sc); }
static int iwx_g_scan_start(void* ctx,const u8* ch,int n){ (void)ch;(void)n; return iwx_scan_start((struct iwx_softc*)ctx,0,0); }
static int iwx_g_scan_done(void* ctx){ (void)ctx; return !iwx_scan_busy(); }
static int iwx_g_join(void* ctx,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht){ (void)akm;(void)ht; return iwx_join_ssid((struct iwx_softc*)ctx,bssid,ch,ssid); }
static void iwx_g_leave(void* ctx){ iwx_leave((struct iwx_softc*)ctx); }
static int iwx_g_tx(void* ctx,const u8* f,int len){ return len<0?-1:iwx_tx_frame((struct iwx_softc*)ctx,f,(u32)len); }
static int iwx_g_set_key(void* ctx,int idx,int type,const u8* key,int len,int pw){ return iwx_set_key((struct iwx_softc*)ctx,idx,type,key,len,pw); }
static int iwx_g_rssi(void* ctx){ (void)ctx; return iwx_last_rssi; }
static void iwx_g_poll(void* ctx){ if(ctx)iwx_rx_poll((struct iwx_softc*)ctx); }
static int iwx_g_mac(void* ctx,u8* mac){ if(!ctx)return -1; iwx_hw_mac((struct iwx_softc*)ctx,mac); return 0; }
static const struct wifi_hw_ops iwx_hw_ops={ "iwx",0,iwx_g_init,iwx_g_scan_start,iwx_g_scan_done,iwx_g_join,iwx_g_leave,iwx_g_tx,iwx_g_set_key,iwx_g_rssi,iwx_g_poll,iwx_g_mac };
int iwx_glue_register(void){ struct iwx_softc* sc=iwx_get_sc(); if(!sc)return -1; return wifi_hw_register(&iwx_hw_ops,sc); }
void iwx_wifi_hw_probe(void){ if(!iwx_probe())return; if(iwx_start()==0)iwx_glue_register(); }
void iwx_up(char* out,int cap){
    if(cap<2)return;
    out[0]=0;
    if(iwx_probe()&&iwx_start()==0)iwx_glue_register();
    iwx_status(out,cap); }
