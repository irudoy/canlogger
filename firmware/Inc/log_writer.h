#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include "fatfs.h"

void lw_init(void);
FRESULT lw_tick(void);
void lw_stop(void);

#endif // LOG_WRITER_H
