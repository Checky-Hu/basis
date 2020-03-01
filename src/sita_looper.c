#include "sita_looper.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum MessageType {
  // User async message.
  USER_ASYNC = 0,
  // User sync message.
  USER_SYNC = 1,
  // Internal exit message.
  INTERNAL_EXIT = 2,
} MessageType;

typedef struct Message {
  // Magic number to check this message.
  unsigned int magic;
  MessageType type;
  // Target execution time.
  struct timespec when;
  // Task from user.
  SitaLooperTask task;
  // Done flag only for sync message.
  bool* done;
  struct Message* next;
} Message;

typedef enum MessageQueueState {
  Unstarted = 0,
  Started = 1,
  Stopped = 2,
  Unknown = 8,
} MessageQueueState;

typedef struct MessageQueue {
  // Magic number to check this queue.
  unsigned int magic;
  // Name only for debug use.
  char name[32];
  // Extra data from user.
  void* user_data;
  // Handle task function from user.
  SitaLooperHandleTaskFunc handle_task_func;
  // Current state of this queue.
  MessageQueueState state;
  // Whether this queue is blocked.
  bool blocked;
  // Thread condition used to block user thread.
  pthread_cond_t user_cond;
  // Thread condition used to block queue thread.
  pthread_cond_t queue_cond;
  // Queue thread.
  pthread_t thread;
  // Mutex used to lock this queue.
  pthread_mutex_t mutex;
  // Max size of this queue.
  size_t capacity;
  // Next index of unused message.
  size_t index;
  // Header of this queue.
  Message* header;
  // Messages pool used to store all messages.
  Message** messages;
} MessageQueue;

#define MESSAGE_QUEUE_DEBUG_ENABLE
#ifdef MESSAGE_QUEUE_DEBUG_ENABLE
#define MESSAGE_QUEUE_DEBUG(format, ...) printf("%s:"format"\n", __func__, ##__VA_ARGS__)
#else
#define MESSAGE_QUEUE_DEBUG(...)
#endif

#define MESSAGE_QUEUE_MAGIC_NUM 0x4855514A //HUQJ
#define MESSAGE_QUEUE_CHECK(queue) \
  do {\
    if (!queue || queue->magic != MESSAGE_QUEUE_MAGIC_NUM) {\
      MESSAGE_QUEUE_DEBUG("invalid queue = 0x%p", (void*)queue);\
      return false;\
    }\
  } while (0)

#define MESSAGE_QUEUE_LOCK(queue) \
  do {\
    if (pthread_mutex_lock(&(queue->mutex))) {\
      MESSAGE_QUEUE_DEBUG("mutex lock failed");\
    }\
  } while (0)

#define MESSAGE_QUEUE_UNLOCK(queue) \
  do {\
    if (pthread_mutex_unlock(&(queue->mutex))) {\
      MESSAGE_QUEUE_DEBUG("mutex unlock failed");\
    }\
  } while (0)

// Alloc an unused message for incoming message.
// No lock in this method since it only called by internal.
static Message* MessageQueueAllocMessage(MessageQueue* queue) {
  Message* message = NULL;
  size_t i;
  for (i = queue->index; i < queue->capacity; i++) {
    if (queue->messages[i]->magic == ~(queue->magic)) {
      message = queue->messages[i];
      break;
    }
  }
  if (!message) {
    for (i = 0; i < queue->index; i ++) {
      if (queue->messages[i]->magic == ~(queue->magic)) {
        message = queue->messages[i];
        break;
      }
    }
  }

  if (message) {
    queue->index += 1;
    if (queue->index == queue->capacity) {
      queue->index = 0;
    }
    message->magic = queue->magic;
  }

  return message;
}

static bool CompareTimeSpec(struct timespec* a,
    struct timespec* b) {
  long a_in_ns = a->tv_sec * 1000000000 + a->tv_nsec;
  long b_in_ns = b->tv_sec * 1000000000 + b->tv_nsec;
  return a_in_ns <= b_in_ns;
}

SitaLooper SitaLooperInit(const char* name,
    size_t capacity,
    SitaLooperHandleTaskFunc handle_task_func,
    void* user_data) {
  if (!name || !capacity || !handle_task_func) {
    MESSAGE_QUEUE_DEBUG("parameter error");
    return NULL;
  }

  // Calculate |MessageQueue| size, the memory layout sames like:
  // |+ |MessageQueue|
  // |+ Array of |Message| pointer
  // |+ Array of |Message|
  size_t offset = sizeof(MessageQueue);
  if (offset & 3) {
    MESSAGE_QUEUE_DEBUG("%lu is not 4 bytes aligned", offset);
    return NULL;
  }
  size_t message_size = (sizeof(Message) + 3) & ~3;
  size_t total_size = offset + (sizeof(Message*) + message_size) * capacity;
  MessageQueue* queue = (MessageQueue*)malloc(total_size);
  if (!queue) {
    MESSAGE_QUEUE_DEBUG("malloc queue failed");
    return NULL;
  }

  queue->magic = MESSAGE_QUEUE_MAGIC_NUM;
  snprintf(queue->name, sizeof(queue->name), "%s-queue", name);
  queue->user_data = user_data;
  queue->handle_task_func = handle_task_func;
  queue->state = Unstarted;
  queue->blocked = false;
  pthread_cond_init(&(queue->user_cond), NULL);
  pthread_cond_init(&(queue->queue_cond), NULL);
  pthread_mutex_init(&(queue->mutex), NULL);
  queue->capacity = capacity;
  queue->index = 0;
  queue->header = NULL;
  // Init messages pool pointer with the index of |Message| pointer array.
  queue->messages = (Message**)((char*)queue + offset);
  // Init each message pointer in the pool with the index in |Message| array.
  offset += sizeof(Message*) * capacity;
  size_t i = 0;
  for (; i < capacity; i++) {
    queue->messages[i] = (Message*)((char*)queue + offset);
    queue->messages[i]->magic = ~(queue->magic);
    offset += message_size;
  }

  return (SitaLooper)queue;
}

bool SitaLooperDeinit(SitaLooper looper) {
  MessageQueue* queue = (MessageQueue*)looper;
  MESSAGE_QUEUE_CHECK(queue);

  MessageQueueState state = Unknown;
  MESSAGE_QUEUE_LOCK(queue);
  state = queue->state;
  MESSAGE_QUEUE_UNLOCK(queue);

  if (state == Stopped) {
    // Queue already stopped, so destroy it.
    pthread_cond_destroy(&(queue->user_cond));
    pthread_cond_destroy(&(queue->queue_cond));
    pthread_mutex_destroy(&(queue->mutex));
    free(queue);
    return true;
  } else {
    // Wrong state of Queue.
    MESSAGE_QUEUE_DEBUG("state = %d is invalid", state);
    return false;
  }
}

static Message* MessageQueueGetNextMessage(MessageQueue* queue) {
  Message* message = NULL;
  // Timeout for getting next message.
  struct timespec timeout;
  do {
    MESSAGE_QUEUE_LOCK(queue);
    queue->blocked = true;
    message = queue->header;
    if (message) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      if (CompareTimeSpec(&(message->when), &now)) {
        // Remove message from queue.
        queue->header = message->next;
        queue->blocked = false;
      } else {
        // Wait until new message coming or |when| of header message arriving.
        timeout.tv_sec = message->when.tv_sec;
        timeout.tv_nsec = message->when.tv_nsec;
      }
    } else {
      // Queue is empty, deadly wait.
      timeout.tv_sec = 0;
    }

    if (queue->blocked) {
      // Wait for new incoming message.
      if (0 == timeout.tv_sec) {
        while (queue->blocked) {
          pthread_cond_wait(&(queue->queue_cond), &(queue->mutex));
        }
      } else {
        pthread_cond_timedwait(&(queue->queue_cond),
            &(queue->mutex), &timeout);
      }
      MESSAGE_QUEUE_UNLOCK(queue);
    } else {
      MESSAGE_QUEUE_UNLOCK(queue);
      break;
    }
  } while (true);
  return message;
}

static void* MessageQueueLoop(void* param) {
  MessageQueue* queue = (MessageQueue*)param;
  MESSAGE_QUEUE_LOCK(queue);
  // Transform state from unstarted to started.
  queue->state = Started;
  // Notify user thread that start progress has done.
  pthread_cond_signal(&(queue->user_cond));
  MESSAGE_QUEUE_UNLOCK(queue);

  while (true) {
    Message* message = MessageQueueGetNextMessage(queue);
    if (message->type == INTERNAL_EXIT) {
      MESSAGE_QUEUE_LOCK(queue);
      queue->state = Stopped;
      MESSAGE_QUEUE_UNLOCK(queue);
      break;
    } else {
      queue->handle_task_func((SitaLooper)queue, &(message->task), queue->user_data);
      MESSAGE_QUEUE_LOCK(queue);
      // Finish sync task, so notify user thread that the task has done.
      if (message->type == USER_SYNC) {
        *(message->done) = true;
        pthread_cond_signal(&(queue->user_cond));
      }
      message->magic = ~(queue->magic);
      MESSAGE_QUEUE_UNLOCK(queue);
    }
  }
  return NULL;
}

static bool MessageQueueSendMessage(MessageQueue* queue,
    MessageType type,
    long delay_ms,
    const SitaLooperTask* task,
    bool* done) {
  MESSAGE_QUEUE_LOCK(queue);
  // Check state first.
  if (queue->state != Started) {
    MESSAGE_QUEUE_DEBUG("state = %d is invalid", queue->state);
    MESSAGE_QUEUE_UNLOCK(queue);
    return false;
  }

  Message* message = MessageQueueAllocMessage(queue);
  if (!message) {
    MESSAGE_QUEUE_DEBUG("alloc message failed");
    MESSAGE_QUEUE_UNLOCK(queue);
    return false;
  }

  // Init |type| field of message.
  message->type = type;
  // Init |when| field of message.
  clock_gettime(CLOCK_MONOTONIC, &(message->when));
  switch (type) {
    case USER_ASYNC:
      message->when.tv_sec += delay_ms / 1000;
      message->when.tv_nsec += (delay_ms % 1000) * 1000000;
      // Init |data| field of message.
      memcpy(&(message->task), task, sizeof(SitaLooperTask));
      break;
    case USER_SYNC:
      // Init |data| field of message.
      memcpy(&(message->task), task, sizeof(SitaLooperTask));
      // Init |done| field of message.
      message->done = done;
    default:
      break;
  }

  bool wakeup = false;
  Message* pre = NULL;
  Message* cur = queue->header;
  while (cur && CompareTimeSpec(&(cur->when), &(message->when))) {
    pre = cur;
    cur = cur->next;
  }
  message->next = cur;
  if (pre) {
    // Insert at middle.
    pre->next = message;
  } else {
    // Insert at head.
    queue->header = message;
    wakeup = queue->blocked;
  }

  if (wakeup) {
    queue->blocked = false;
    pthread_cond_signal(&(queue->queue_cond));
  }
  MESSAGE_QUEUE_UNLOCK(queue);

  return true;
}

bool SitaLooperStart(SitaLooper looper) {
  MessageQueue* queue = (MessageQueue*)looper;
  MESSAGE_QUEUE_CHECK(queue);

  bool ret = false;
  MESSAGE_QUEUE_LOCK(queue);
  if (queue->state == Unstarted) {
    // Queue is unstarted, so start it.
    if (pthread_create(&(queue->thread), NULL, MessageQueueLoop, queue)) {
      MESSAGE_QUEUE_DEBUG("can't create thread");
    } else {
      // Wait until the queue thread notify that start progress has done.
      while (queue->state != Started) {
        pthread_cond_wait(&(queue->user_cond), &(queue->mutex));
      }
      ret = true;
    }
  } else {
    // Wrong state of queue.
    MESSAGE_QUEUE_DEBUG("queue->state = %d is invalid\n", queue->state);
  }
  MESSAGE_QUEUE_UNLOCK(queue);
  return ret;
}

bool SitaLooperStop(SitaLooper looper) {
  MessageQueue* queue = (MessageQueue*)looper;
  MESSAGE_QUEUE_CHECK(queue);

  MessageQueueState state = Unknown;
  MESSAGE_QUEUE_LOCK(queue);
  state = queue->state;
  MESSAGE_QUEUE_UNLOCK(queue);

  if (state == Started) {
    // Queue is started, so stop it.
    MessageQueueSendMessage(queue, INTERNAL_EXIT, 0, NULL, NULL);
    pthread_join(queue->thread, NULL);
    return true;
  } else {
    // Wrong state of queue.
    MESSAGE_QUEUE_DEBUG("state = %d is invalid", state);
    return false;
  }
}

bool SitaLooperExecSyncTask(SitaLooper looper, const SitaLooperTask* task) {
  MessageQueue* queue = (MessageQueue*)looper;
  MESSAGE_QUEUE_CHECK(queue);
  bool done = false;
  if (MessageQueueSendMessage(queue, USER_SYNC, 0, task, &done)) {
    // Wait until the queue thread notify that sync task has done.
    MESSAGE_QUEUE_LOCK(queue);
    while (!done) {
      pthread_cond_wait(&(queue->user_cond), &(queue->mutex));
    }
    MESSAGE_QUEUE_UNLOCK(queue);
    return true;
  } else {
    MESSAGE_QUEUE_DEBUG("internal error of looper");
    return false;
  }
}

bool SitaLooperPostAsyncTask(SitaLooper looper,
    const SitaLooperTask* task, long delay_ms) {
  MessageQueue* queue = (MessageQueue*)looper;
  MESSAGE_QUEUE_CHECK(queue);
  return MessageQueueSendMessage(queue, USER_ASYNC, delay_ms, task, NULL);
}

