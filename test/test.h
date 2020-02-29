#ifndef TEST_H_
#define TEST_H_

#include <stdbool.h>
#include <stdio.h>

#define TEST_DEBUG(format, ...) printf("%s:"format"\n", __func__, ##__VA_ARGS__)

#define TEST_ASSERT(expression, words) \
  do {\
    if (!(expression)) {\
      TEST_DEBUG(words);\
      return false;\
    }\
  } while (0)

#define TEST_UNUSED_PARAM(param) (param = param)

#endif

