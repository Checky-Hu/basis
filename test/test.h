#ifndef TEST_H_
#define TEST_H_

#include <stdbool.h>
#include <stdio.h>

#define TEST_DEBUG(args...) printf(args)

#define TEST_ASSERT(expression, words) \
  do {\
    if (!(expression)) {\
      TEST_DEBUG("%s: %s\n", __FUNCTION__, words);\
      return false;\
    }\
  } while (0)

#endif

