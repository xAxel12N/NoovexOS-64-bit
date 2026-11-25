#!/bin/sh
cd "$(dirname "$0")/../../../.." && python3 tools/tests/wifi/fastrc/fastrc_gen.py net/wifi/wifi_mgr.c include/nvx/drivers/net/wifi/rtw8821ce.h tools/tests/wifi/fastrc/fastrc_main.c /tmp/fastrc_t.c && gcc -w -fsanitize=address,undefined -g /tmp/fastrc_t.c -o /tmp/fastrc_t && /tmp/fastrc_t
