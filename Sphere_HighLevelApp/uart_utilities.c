#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <applibs/log.h>
#include "uart_utilities.h"


static int engineTemp;
static int engineRpm;
static int fuel;

char rxData[20];
char rxIndex = 0;

void getResponse(int uartFd, uint8_t *receiveBuffer, ssize_t receiveBufferSize) {
    //uint8_t receiveBuffer[receiveBufferSize+1]; // allow extra byte for string termination
    ssize_t bytesRead = 0;
    ssize_t remainbytes = receiveBufferSize-2;
   

    
    // Read incoming UART data. It is expected behavior that messages may be received in multiple
    // partial chunks.
    bytesRead = read(uartFd, receiveBuffer, remainbytes);
    if (bytesRead == -1) {
        Log_Debug("ERROR: Could not read UART: %s (%d).\n", strerror(errno), errno);
        exitCode = ExitCode_UartEvent_Read;
        return;
    }

    if (bytesRead > 0) {
        // Null terminate the buffer to make it a valid string, and print it
        receiveBuffer[bytesRead] = 0;
        Log_Debug("UART received %d bytes: '%s'.\n", bytesRead, receiveBuffer);

    }

}

/// <summary>
///     Handle UART event: if there is incoming data, print it.
///     This satisfies the EventLoopIoCallback signature.
/// </summary>
void UartEventHandler(EventLoop* el, int fd, EventLoop_IoEvents events, void* context)
{
    const size_t receiveBufferSize = 20;
    uint8_t receiveBuffer[receiveBufferSize + 1]; // allow extra byte for string termination
    ssize_t bytesRead;


    // Read incoming UART data. It is expected behavior that messages may be received in multiple
    // partial chunks.
    bytesRead = read(fd, receiveBuffer, receiveBufferSize);
    if (bytesRead == -1) {
        Log_Debug("ERROR: Could not read UART: %s (%d).\n", strerror(errno), errno);
        exitCode = ExitCode_UartEvent_Read;
        return;
    }

    if (bytesRead > 0) {
        if ((char*)receiveBuffer[bytesRead - 1] == '\r');
        {
            // Null terminate the buffer to make it a valid string, and print it
            receiveBuffer[bytesRead] = 0;
            Log_Debug("UART received %d bytes: '%s'.\n", bytesRead, (char*)receiveBuffer);

        }
    }
    
}

void test123(void)
{
    char* payload;
    char* response;

    //ssize_t bytesSent = write(uartFd, "0105", strlen("0105"));
    //getResponse();
    //getResponse();
    //response = getResponse();
    //engineTemp = strtol(response[6], 0, 16);
    //Log_Debug("Coolant Temp: %d", engineTemp);

    //bytesSent = write(uartFd, "010C", strlen("010C"));
    //getResponse();
    //getResponse();
    //response = getResponse();
    //engineRpm = ((strtol(response[6], 0, 16) * 256) + strtol(response[9], 0, 16)) / 4;
    //Log_Debug("Engine RPM: %d", engineRpm);

    //bytesSent = write(uartFd, "012F", strlen("012F"));
    //getResponse();
    //getResponse();
    //response = getResponse();
    //fuel = strtol(response[6], 0, 16);
    //fuel = 1.0 * fuel / 255 * 100; // in the scale of 100
    //Log_Debug("Engine Fuel: %d", fuel);

}