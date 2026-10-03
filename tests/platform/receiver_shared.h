#ifndef POINTY_RECEIVER_SHARED_H
#define POINTY_RECEIVER_SHARED_H
#include <windows.h>
typedef struct ReceiverState {
    ULONG_PTR window;
    volatile LONG clicks, right_clicks, wheels;
} ReceiverState;
#endif
