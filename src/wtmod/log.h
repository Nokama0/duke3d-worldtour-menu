// LOGI/LOGE map onto wt_log so the copied config.cpp's log lines reach the port's log (no severity column).
#ifndef WTMOD_LOG_H
#define WTMOD_LOG_H
#include "hook.h"

#define LOGI(...) wt_log(__VA_ARGS__)
#define LOGE(...) wt_log(__VA_ARGS__)

#endif
