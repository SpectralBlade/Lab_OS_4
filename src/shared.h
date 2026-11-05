#ifndef LAB_OC_04_SHARED_H
#define LAB_OC_04_SHARED_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum {
    MESSAGE_RESULT = 1,
    MESSAGE_DONE = 2,
    MESSAGE_ERROR = 3
};

typedef struct {
    LONG kind;
    LONG sum;
    DWORD line;
} SharedData;

#endif
