#ifndef NVX_NET_STACK_FIREWALL_H
#define NVX_NET_STACK_FIREWALL_H

#include "include/nvx/base.h"

#define FW_RULES 16
#define FW_CT 32
#define FW_CONF "/etc/firewall.conf"

struct fw_rule { u8 proto, used; u16 port; char svc[24]; };

extern int g_fw_on;
extern u32 g_fw_dropped, g_fw_passed;
extern struct fw_rule g_fw_rule[FW_RULES];

void fw_defaults(void);
int fw_parse(const char* t,int n);
int fw_format(char* out,int cap);
void fw_load(void);
int fw_save(void);
int fw_set(int on);
int fw_allow(int proto,int port,const char* svc);
int fw_deny(int proto,int port);
int fw_rule_open(const struct fw_rule* r);
int fw_allow_in(int proto,u32 sip,u16 sport,u16 dport,int local);
void fw_note_out(int proto,u16 lport,u32 rip,u16 rport);
int fw_proto(const char* s);
int fw_status(char* out,int cap);

#endif
