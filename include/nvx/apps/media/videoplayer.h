#ifndef NVX_APPS_VIDEOPLAYER_H
#define NVX_APPS_VIDEOPLAYER_H

int video_open_url(const char* url, const char* title);
int video_open_slot(int slot);
int video_is_media_name(const char* name);
void video_draw(int i);
void video_click(int px, int py);
void video_key(int c);
int video_tick(void);
int video_fast(void);
void video_stop(void);
void media_blit(int id, int x, int y, int w, int h, u32 bg);

#endif
