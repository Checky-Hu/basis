#ifndef SITA_LOOPER_H_
#define SITA_LOOPER_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SitaLooper* SitaLooper;

typedef struct {
  int what;
  void* obj;
} SitaLooperTask;

typedef void (*SitaLooperHandleTaskFunc)(SitaLooper looper,
    const SitaLooperTask* task, void* user_data);

SitaLooper SitaLooperInit(const char* name, size_t capacity,
    SitaLooperHandleTaskFunc handle_task_func, void* user_data);

bool SitaLooperDeinit(SitaLooper looper);

bool SitaLooperStart(SitaLooper looper);

bool SitaLooperStop(SitaLooper looper);

bool SitaLooperExecSyncTask(SitaLooper looper, const SitaLooperTask* task);

bool SitaLooperPostAsyncTask(SitaLooper looper,
    const SitaLooperTask* task, long delay_ms);

#ifdef __cplusplus
}
#endif

#endif

