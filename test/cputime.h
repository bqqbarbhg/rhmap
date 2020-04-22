#pragma once

#include <stdint.h>

#ifdef __cplusplus
	extern "C" {
#endif

typedef struct {
	uint64_t os_tick;
	uint64_t cpu_tick;
} cputime_sync_point;

typedef struct {
	cputime_sync_point begin, end;
	uint64_t os_freq;
	uint64_t cpu_freq;
	double rcp_os_freq;
	double rcp_cpu_freq;
} cputime_sync_span;

extern const cputime_sync_span *cputime_default_sync;

void cputime_begin_init();
void cputime_end_init();
void cputime_init();

void cputime_begin_sync(cputime_sync_span *span);
void cputime_end_sync(cputime_sync_span *span);

uint64_t cputime_cpu_tick();
uint64_t cputime_os_tick();

double cputime_cpu_delta_to_sec(const cputime_sync_span *span, uint64_t cpu_delta);
double cputime_os_delta_to_sec(const cputime_sync_span *span, uint64_t os_delta);
double cputime_cpu_tick_to_sec(const cputime_sync_span *span, uint64_t cpu_tick);
double cputime_os_tick_to_sec(const cputime_sync_span *span, uint64_t os_tick);

#ifdef __cplusplus
	}
#endif
