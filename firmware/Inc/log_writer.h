#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include "main.h"
#include "fatfs.h"
#include "stdio.h"

void lw_init(void);
void lw_tick(void);
void lw_stop(void);

#endif // LOG_WRITER_H
