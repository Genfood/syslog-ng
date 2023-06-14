#ifndef SLOG_DEST_H_INCLUDED
#define SLOG_DEST_H_INCLUDED

#include "driver.h"

LogDriver *slog_dd_new(GlobalConfig *cfg);

void slog_dd_set_filename(LogDriver *d, const gchar *filename);

#endif
