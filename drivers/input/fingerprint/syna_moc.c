/*
 * Copyright (C) 2019 Synaptics Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * NoovexOS port of libfprint drivers/synaptics (synaptics.c, bmkt_message.c, bmkt.h, bmkt_response.h):
 * used under GPL-3.0-or-later as permitted by LGPL-2.1 section 3.
 */
#include "include/nvx/base.h"
#include "include/nvx/drivers/input/fingerprint/fprint.h"
#define SY_OUT 0x01
#define SY_IN 0x81
#define SY_INT 0x83
#define SY_ACE 0xA7
#define SY_ASYNC 0xA8
#define SY_HDR 0xFE
#define SY_MAXP 250
static struct { fp_xport_t x; int ok; u8 seq, cur; u8 rx[272]; int rid, rn, res; const u8* rp; } sy;
static int sy_tx(u8 id,const u8* pl,int n){
    u8 b[SY_MAXP+5]; if(n<0||n>SY_MAXP)return -1;
    sy.seq=(u8)(sy.seq>=255?1:sy.seq+1); sy.cur=sy.seq;
    b[0]=SY_ACE; b[1]=SY_HDR; b[2]=sy.seq; b[3]=id; b[4]=(u8)n; for(int i=0;i<n;i++)b[5+i]=pl[i];
    return sy.x.out(sy.x.ctx,SY_OUT,b,n+5,1000)==n+5?0:-1; }
static int sy_rx(int ms){
    int g=sy.x.in(sy.x.ctx,SY_IN,sy.rx,(int)sizeof sy.rx,ms); if(g<6||sy.rx[2]!=SY_HDR)return -1;
    int pn=sy.rx[5]; if(pn>g-6)return -1;
    sy.rid=sy.rx[4]; sy.rn=pn; sy.rp=sy.rx+6; sy.res=pn==2?(sy.rx[6]<<8|sy.rx[7]):0;
    return sy.rx[3]; }
static int sy_fail_id(int id){
    switch(id){ case 0x02: case 0x09: case 0x12: case 0x22: case 0x32: case 0x35: case 0x43: case 0x58: case 0x63: case 0x67: case 0x79: case 0x82: case 0x85: case 0x93: case 0xA3: case 0xB3: case 0xC3: case 0xC6: case 0xD3: case 0xE5: return 1; }
    return 0; }
int synafp_complete(int id){
    if(sy_fail_id(id))return 1;
    switch(id){ case 0x13: case 0x23: case 0x33: case 0x36: case 0x42: case 0x59: case 0x64: case 0x68: case 0x76: case 0x77: case 0x78: case 0x83: case 0x86: case 0xA2: case 0xB2: return 1; }
    return 0; }
typedef int (*sy_cb)(void* u);
static struct { int s, steps, budget, pms, fresh; sy_cb cb; void* u; } syc;
static int sy_start(u8 id,const u8* pl,int n,int total_ms,int pms,sy_cb cb,void* u){
    if(!sy.ok||sy_tx(id,pl,n))return -1;
    syc.s=sy_rx(1000); syc.steps=0; syc.pms=pms; syc.budget=total_ms/pms<1?1:total_ms/pms; syc.cb=cb; syc.u=u; syc.fresh=1; return 0; }
static int sy_step(void){
    if(syc.fresh){ syc.fresh=0; if(syc.s<0)return -1;
        if(syc.s==sy.cur){ int r=syc.cb?syc.cb(syc.u):0; if(r)return r; if(synafp_complete(sy.rid))return sy_fail_id(sy.rid)?-2:1; }
        if(++syc.steps>200)return -1; }
    if(syc.budget<=0){ return -1; } syc.budget--;
    u8 ib[8]; int g=sy.x.in(sy.x.ctx,SY_INT,ib,7,syc.pms); if(!(g>0&&(ib[0]&4)))return syc.budget>0?0:-1;
    u8 c=SY_ASYNC; if(sy.x.out(sy.x.ctx,SY_OUT,&c,1,1000)!=1)return -1;
    syc.s=sy_rx(1000); syc.fresh=1; return 0; }
static int sy_cmd(u8 id,const u8* pl,int n,int total_ms,sy_cb cb,void* u){
    if(sy_start(id,pl,n,total_ms,100,cb,u))return -1;
    int r; while(!(r=sy_step())); return r; }
int synafp_open(const fp_xport_t* x){ sy.x=*x; sy.ok=1; sy.seq=0; return 1; }
int synafp_init(void){ int r=sy_cmd(0x11,0,0,2000,0,0); return r==1&&sy.rid==0x13; }
int synafp_result(void){ return sy.res; }
static int sy_uid(const char* uid,u8* out,int cap){ int n=0; while(uid[n]&&n<cap)n++; for(int i=0;i<n;i++)out[i]=(u8)uid[i]; return n; }
struct sy_enr { void (*prog)(int); int pct; };
static int sy_enr_cb(void* u){ struct sy_enr* e=(struct sy_enr*)u; if(sy.rid==0x55&&sy.rn==1){ e->pct=sy.rp[0]; if(e->prog)e->prog(e->pct); } return 0; }
int synafp_enroll(const char* uid,int finger,void (*prog)(int pct)){
    u8 p[SY_MAXP]; int n=sy_uid(uid,p+2,100); p[0]=0; p[1]=(u8)finger;
    struct sy_enr e; e.prog=prog; e.pct=0;
    int r=sy_cmd(0x51,p,n+2,60000,sy_enr_cb,&e);
    if(r==1&&sy.rid==0x59)return 1;
    return r==-2&&sy.res==501?-2:0; }
static struct sy_enr sye;
int synafp_enroll_begin(const char* uid,int finger){
    u8 p[SY_MAXP]; int n=sy_uid(uid,p+2,100); p[0]=0; p[1]=(u8)finger; sye.prog=0; sye.pct=0;
    return sy_start(0x51,p,n+2,60000,50,sy_enr_cb,&sye); }
int synafp_enroll_step(int* pct){
    int r=sy_step(); if(pct)*pct=sye.pct; if(!r)return 0;
    if(r==1&&sy.rid==0x59)return 1;
    return r==-2&&sy.res==501?-2:-1; }
int synafp_verify(const char* uid){
    u8 p[SY_MAXP]; int n=sy_uid(uid,p,100);
    int r=sy_cmd(0x65,p,n,30000,0,0);
    if(r==1&&sy.rid==0x68&&sy.rn>=3+n){ for(int i=0;i<n;i++) if(sy.rp[3+i]!=p[i])return 0; for(int i=3+n;i<sy.rn;i++) if(sy.rp[i])return 0; return 1; }
    if(r==-2&&sy.res==404)return 0;
    if(r==-2&&sy.res==213)return -3;
    if(r==-2&&sy.res==504)return -2;
    return -1; }
int synafp_delete(const char* uid,int finger){
    u8 p[SY_MAXP]; int n=sy_uid(uid,p+1,100); p[0]=(u8)finger;
    int r=sy_cmd(0x81,p,n+1,5000,0,0); return r==1&&sy.rid==0x83; }
