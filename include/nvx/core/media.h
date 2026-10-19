#ifndef NVX_CORE_MEDIA_H
#define NVX_CORE_MEDIA_H

#include "include/nvx/base.h"

#define MEDIA_MAX 8

#define MEDIA_CMD_PLAY 1
#define MEDIA_CMD_PAUSE 2
#define MEDIA_CMD_SEEK 3
#define MEDIA_CMD_VOLUME 4
#define MEDIA_CMD_SIZE 5
#define MEDIA_CMD_QUIT 6
#define MEDIA_CMD_LOOP 7
#define MEDIA_CMD_DURATION 8

#define MEDIA_ST_PLAYING 1
#define MEDIA_ST_PAUSED 2
#define MEDIA_ST_ENDED 4
#define MEDIA_ST_ERROR 8
#define MEDIA_ST_VIDEO 16
#define MEDIA_ST_AUDIO 32
#define MEDIA_ST_WAITING 64
#define MEDIA_ST_READY 128

struct media_status { i64 cur_ms, dur_ms; u32 flags, vw, vh, buffered_pct; };

int media_new(void);
int media_start(int id, const char* url, int split, int w, int h, i64 start_ms, int paused);
int media_append(int id, int stream, const u8* d, int len);
void media_end_stream(int id, int stream);
void media_cmd(int id, int cmd, i64 a, i64 b);
int media_get_status(int id, struct media_status* out);
const u32* media_frame(int id, int* w, int* h, u64* seq);
void media_free(int id);
int media_restart(int id, i64 start_ms, int keep_data);
int media_alive(int id);
u64 media_stream_len(int id, int stream);
void media_tick(void);
long media_ioctl(int vfidx, u64 req, u64 arg);
void media_vf_close(int vfidx);

#endif
