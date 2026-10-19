#include "quakedef.h"

#define QSN_SAMPLES 32768

static short qsn_buf[QSN_SAMPLES];
static int qsn_rate = 0;
static int qsn_on = 0;
static unsigned long qsn_read = 0;

void qsh_set_audio(int rate)
{
	qsn_rate = rate;
}

qboolean SNDDMA_Init(void)
{
	if (qsn_rate < 8000)
		return false;
	memset(qsn_buf, 0, sizeof(qsn_buf));
	shm = &sn;
	shm->splitbuffer = 0;
	shm->samplebits = 16;
	shm->speed = qsn_rate;
	shm->channels = 2;
	shm->samples = QSN_SAMPLES;
	shm->samplepos = 0;
	shm->soundalive = true;
	shm->gamealive = true;
	shm->submission_chunk = 1;
	shm->buffer = (unsigned char *)qsn_buf;
	qsn_read = 0;
	qsn_on = 1;
	return true;
}

int SNDDMA_GetDMAPos(void)
{
	if (!qsn_on)
		return 0;
	shm->samplepos = (int)(qsn_read & (QSN_SAMPLES - 1));
	return shm->samplepos;
}

void SNDDMA_Shutdown(void)
{
	qsn_on = 0;
}

void SNDDMA_Submit(void)
{
}

int qsh_audio_pull(short *out, int frames)
{
	if (!qsn_on || frames <= 0)
		return 0;
	if (frames > QSN_SAMPLES / 4)
		frames = QSN_SAMPLES / 4;
	for (int i = 0; i < frames * 2; i++) {
		int p = (int)((qsn_read + (unsigned long)i) & (QSN_SAMPLES - 1));
		out[i] = qsn_buf[p];
		qsn_buf[p] = 0;
	}
	qsn_read += (unsigned long)frames * 2;
	return frames;
}
