#include "test.h"

#include "sita_looper.h"

static void HandleTask(SitaLooper looper,
    const SitaLooperTask* task,
    void* user_data) {
  TEST_DEBUG("%s: %d\n", __FUNCTION__, task->what);
}

static bool TestSitaLooper() {
  SitaLooper looper = SitaLooperInit("test", 5, HandleTask, NULL);
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
  return true;
}

int main(int argc, char ** argv) {
  if (TestSitaLooper()) {
    TEST_DEBUG("TestSitaLooper success\n");
  } else {
    TEST_DEBUG("TestSitaLooper fail\n");
  }
  return 0;
}
