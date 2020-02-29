#include <unistd.h>

#include "sita_looper.h"
#include "test.h"

static size_t index;
static void HandleTask(SitaLooper looper,
    const SitaLooperTask* task,
    void* user_data) {
  TEST_UNUSED_PARAM(looper);
  TEST_DEBUG("what = %d", task->what);
  ((int*)user_data)[index] = task->what;
  index += 1;
}

static bool TestSitaLooper1() {
#undef LEN
#define LEN 2
  int result[LEN] = {0, 0};
  const int target[LEN] = {0, 2};

  SitaLooper looper = SitaLooperInit("test", 5, HandleTask, result);
  TEST_ASSERT(looper, "looper init failed");

  TEST_ASSERT(SitaLooperStart(looper), "looper start failed");

  SitaLooperTask task;
  task.what = 0;
  task.obj = NULL;
  TEST_ASSERT(SitaLooperPostAsyncTask(looper, &task, 0),
      "looper post async task failed");

  task.what = 1;
  TEST_ASSERT(SitaLooperPostAsyncTask(looper, &task, 100),
      "looper post async task failed");

  task.what = 2;
  TEST_ASSERT(SitaLooperExecSyncTask(looper, &task),
      "looper exec sync task failed");

  TEST_ASSERT(SitaLooperStop(looper), "looper stop failed");

  TEST_ASSERT(SitaLooperDeinit(looper), "looper deinit failed");

  for (size_t i = 0; i < LEN; i++) {
    if (target[i] != result[i]) {
      return false;
    }
  }
  return true;
}

static bool TestSitaLooper2() {
#undef LEN
#define LEN 3
  int result[LEN] = {0, 0, 0};
  const int target[LEN] = {0, 2, 1};

  SitaLooper looper = SitaLooperInit("test", 5, HandleTask, result);
  TEST_ASSERT(looper, "looper init failed");

  TEST_ASSERT(SitaLooperStart(looper), "looper start failed");

  SitaLooperTask task;
  task.what = 0;
  task.obj = NULL;
  TEST_ASSERT(SitaLooperPostAsyncTask(looper, &task, 0),
      "looper post async task failed");

  task.what = 1;
  TEST_ASSERT(SitaLooperPostAsyncTask(looper, &task, 100),
      "looper post async task failed");

  task.what = 2;
  TEST_ASSERT(SitaLooperExecSyncTask(looper, &task),
      "looper exec sync task failed");

  usleep(300000);
  TEST_ASSERT(SitaLooperStop(looper), "looper stop failed");

  TEST_ASSERT(SitaLooperDeinit(looper), "looper deinit failed");

  for (size_t i = 0; i < LEN; i++) {
    if (target[i] != result[i]) {
      return false;
    }
  }
  return true;
}

int main(int argc, char ** argv) {
  TEST_UNUSED_PARAM(argc);
  TEST_UNUSED_PARAM(argv);
  index = 0;
  if (TestSitaLooper1()) {
    TEST_DEBUG("TestSitaLooper1 success");
  } else {
    TEST_DEBUG("TestSitaLooper1 fail");
  }
  index = 0;
  if (TestSitaLooper2()) {
    TEST_DEBUG("TestSitaLooper2 success");
  } else {
    TEST_DEBUG("TestSitaLooper2 fail");
  }
  return 0;
}
