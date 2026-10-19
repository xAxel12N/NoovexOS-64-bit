#ifndef NVX_HTTPIO_H
#define NVX_HTTPIO_H
typedef struct hio hio;
hio* hio_open(const char* url);
int hio_read(hio* h, unsigned char* b, int n);
long long hio_seek(hio* h, long long pos);
long long hio_size(hio* h);
long long hio_tell(hio* h);
int hio_seekable(hio* h);
void hio_close(hio* h);
#endif
