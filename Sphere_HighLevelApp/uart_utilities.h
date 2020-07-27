#include "applibs_versions.h"
#include <applibs/uart.h>
#include <applibs/eventloop.h>
#include "common.h"


void getResponse(int uartFd, uint8_t* receiveBuffer, ssize_t receiveBufferSize);

void UartEventHandler(EventLoop* el, int fd, EventLoop_IoEvents events, void* context);

void test123(void);
