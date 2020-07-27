/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

// This sample C application for Azure Sphere demonstrates how to use a UART (serial port).
// The sample opens a UART with a baud rate of 115200. Pressing a button causes characters
// to be sent from the device over the UART; data received by the device from the UART is echoed to
// the Visual Studio Output Window.
//
// It uses the API for the following Azure Sphere application libraries:
// - UART (serial port)
// - GPIO (digital input for button)
// - log (messages shown in Visual Studio's Device Output window during debugging)
// - eventloop (system invokes handlers for timer events)

#include <errno.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/uart.h>
#include <applibs/gpio.h>
#include <applibs/log.h>
#include <applibs/eventloop.h>

//#include "uart_utilities.h"

#include "config_structs.h"
#include "mqtt_utilities.h"
#include "common.h"

// By default, this sample targets hardware that follows the MT3620 Reference
// Development Board (RDB) specification, such as the MT3620 Dev Kit from
// Seeed Studio.
//
// To target different hardware, you'll need to update CMakeLists.txt. See
// https://github.com/Azure/azure-sphere-samples/tree/master/Hardware for more details.
//
// This #include imports the sample_hardware abstraction from that hardware definition.
#include <hw/sample_hardware.h>

#include "eventloop_timer_utilities.h"

//MQTT Variable
#define MQTT_PUBLISH_PERIOD 10
static EventLoopTimer* publishMQTTMsgTimer = NULL;
static char* mqttConf_topic = "vehicle/telemetry";
static char* mqttConf_brokerIp = "10.38.1.129";

//MQTT Variable
#define UART_SEND_PERIOD 5
static EventLoopTimer* sendUartTimer = NULL;
const size_t receiveBufferSize = 20;

// File descriptors - initialized to invalid value

static int gpioButtonFd = -1;
static int uartFd = -1;

EventLoop *eventLoop = NULL;
EventRegistration* uartEventReg = NULL;
EventLoopTimer *buttonPollTimer = NULL;

// State variables
static GPIO_Value_Type buttonState = GPIO_Value_High;



const char* engineRPMCmd = "010C";
const char* engineTempCmd = "0105";
const char* engineFuelCmd = "012F";

static int engineRPM;
static int engineTemp;
static int engineFuel;


static char rxData[20];
static char rxIndex = 0;



static void TerminationHandler(int signalNumber);
static void SendUartMessage(int uartFd, const char *dataToSend);
static void ButtonTimerEventHandler(EventLoopTimer *timer);

static ExitCode InitPeripheralsAndHandlers(void);
static void CloseFdAndPrintError(int fd, const char *fdName);
static void ClosePeripheralsAndHandlers(void);

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
    // Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
    exitCode = ExitCode_TermHandler_SigTerm;
}

/// <summary>
///     Helper function to send a fixed message via the given UART.
/// </summary>
/// <param name="uartFd">The open file descriptor of the UART to write to</param>
/// <param name="dataToSend">The data to send over the UART</param>
static void SendUartMessage(int uartFd, const char *dataToSend)
{

    size_t totalBytesSent = 0;
    size_t totalBytesToSend = strlen(dataToSend);
    int sendIterations = 0;
    while (totalBytesSent < totalBytesToSend) {
        sendIterations++;

        // Send as much of the remaining data as possible
        size_t bytesLeftToSend = totalBytesToSend - totalBytesSent;
        const char* remainingMessageToSend = dataToSend + totalBytesSent;
        ssize_t bytesSent = write(uartFd, remainingMessageToSend, bytesLeftToSend);
        if (bytesSent == -1) {
            Log_Debug("ERROR: Could not write to UART: %s (%d).\n", strerror(errno), errno);
            exitCode = ExitCode_SendMessage_Write;
            return;
        }

        totalBytesSent += (size_t)bytesSent;
    }

    Log_Debug("Sent %zu bytes over UART in %d calls.\n", totalBytesSent, sendIterations);

}

static void getResponse(int fd)
{
    const size_t receiveBufferSize = 20;
    uint8_t receiveBuffer[receiveBufferSize + 1]; // allow extra byte for string termination
    ssize_t bytesRead;

    while (1)
    {
        // Read incoming UART data. It is expected behavior that messages may be received in multiple
        // partial chunks.
        bytesRead = read(fd, receiveBuffer, receiveBufferSize);
        if (bytesRead == -1) {
            Log_Debug("ERROR: Could not read UART: %s (%d).\n", strerror(errno), errno);
            exitCode = ExitCode_UartEvent_Read;
            return;
        }

        if (bytesRead > 0) {
            // Null terminate the buffer to make it a valid string, and print it
            receiveBuffer[bytesRead] = 0;
            Log_Debug("UART received %d bytes: '%s'.\n", bytesRead, (char*)receiveBuffer);

            for (int i = 0; i < bytesRead; i++)
            {
                rxData[rxIndex] = receiveBuffer[i];
                rxIndex++;
            }
        }
        if (receiveBuffer[bytesRead - 1] == 0x0D) //detect return
        {
            rxData[rxIndex - 1] = 0;
            Log_Debug("Full UART received: '%s'.\n", (char*)rxData);
            rxIndex = 0;
            return;
        }
    }
}
static void mqttPublish(void)
{

    if (!MQTTIsActiveConnection()) {

        int res = MQTTInit(mqttConf_brokerIp, "1883", mqttConf_topic);
    }

    char buf[100];
    snprintf(buf, 100, "{\"engineTemp\":\"%d\",\"engineRpm\":\"%d\",\"fuel\":\"%d\"}\n", engineTemp, engineRPM, engineFuel);
    if (MQTTPublish(mqttConf_topic, buf) != 0) {
        MQTTStop();
    }
}
/// <summary>
///     Handle button timer event: if the button is pressed, send data over the UART.
/// </summary>
static void ButtonTimerEventHandler(EventLoopTimer *timer)
{
    uint8_t response[receiveBufferSize + 1];
    if (ConsumeEventLoopTimerEvent(timer) != 0) {
        exitCode = ExitCode_ButtonTimer_Consume;
        return;
    }

    // Check for a button press
    GPIO_Value_Type newButtonState;
    int result = GPIO_GetValue(gpioButtonFd, &newButtonState);
    if (result != 0) {
        Log_Debug("ERROR: Could not read button GPIO: %s (%d).\n", strerror(errno), errno);
        exitCode = ExitCode_ButtonTimer_GetValue;
        return;
    }

    // If the button has just been pressed, send data over the UART
    // The button has GPIO_Value_Low when pressed and GPIO_Value_High when released
    if (newButtonState != buttonState) {
        if (newButtonState == GPIO_Value_Low) {
            //SendUartMessage(uartFd, "Hello world!\n");

            SendUartMessage(uartFd, engineRPMCmd);
            Log_Debug("Waiting Engine RPM\n");
            getResponse(uartFd);
            //getResponse(uartFd);
            //getResponse(uartFd);
            engineRPM = ((strtol(&rxData[6], 0, 16) * 256) + strtol(&rxData[9], 0, 16)) / 4;

            //getResponse(uartFd, &response, (receiveBufferSize + 1));
            Log_Debug("Engine RPM : %d RPM\n", engineRPM);
            memset(rxData, 0, sizeof(rxData)); //reset buffer

            SendUartMessage(uartFd, engineTempCmd);
            Log_Debug("Waiting Engine Temp\n");
            getResponse(uartFd);
            //getResponse(uartFd);
            //getResponse(uartFd);
            engineTemp = strtol(&rxData[6], 0, 16);
            engineTemp = engineTemp - 40;
            //getResponse(uartFd, &response, (receiveBufferSize + 1));
            Log_Debug("Engine Temp : %dC\n", engineTemp);
            memset(rxData, 0, sizeof(rxData)); //reset buffer

            SendUartMessage(uartFd, engineFuelCmd);
            Log_Debug("Waiting Engine Fuel\n");
            getResponse(uartFd);
            //getResponse(uartFd);
            //getResponse(uartFd);
            engineFuel = strtol(&rxData[6], 0, 16); // in the scale of 255
            engineFuel = 1.0 * engineFuel / 255 * 100; // in the scale of 100

            //getResponse(uartFd, &response, (receiveBufferSize + 1));
            Log_Debug("Engine Fuel : %d%%\n", engineFuel);
            memset(rxData, 0, sizeof(rxData)); //reset buffer
  
            mqttPublish();
        }
        buttonState = newButtonState;
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
            // Null terminate the buffer to make it a valid string, and print it
            receiveBuffer[bytesRead] = 0;
            Log_Debug("UART received %d bytes: '%s'.\n", bytesRead, (char*)receiveBuffer);

            for (int i = 0; i < bytesRead; i++)
            {
                rxData[rxIndex] = receiveBuffer[i];
                rxIndex++;
            }
    }
    if (receiveBuffer[bytesRead - 1] == 0x0D) //detect return
    {
        rxData[rxIndex - 1] = 0;
        Log_Debug("UART received %d bytes: '%s'.\n", bytesRead, (char*)rxData);

        rxIndex = 0;

    }

}

//static void mqttPublishTimerEventHandler(EventLoopTimer* timer)
//{
//
//    if (ConsumeEventLoopTimerEvent(timer) != 0) {
//        exitCode = ExitCode_TimerHandler_Consume;
//        return;
//    }
//
//    if (!MQTTIsActiveConnection()) {
//        int res = MQTTInit(mqttConf_brokerIp, "1883", mqttConf_topic);
//    }
//
//    char buf[100];
//    snprintf(buf, 100, "{\"engineTemp\":\"%d\",\"engineRpm\":\"%d\",\"fuel\":\"%d\"}\n" ,engineTemp,engineRPM,engineFuel);
//
//}

/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>
///     ExitCode_Success if all resources were allocated successfully; otherwise another
///     ExitCode value which indicates the specific failure.
/// </returns>
static ExitCode InitPeripheralsAndHandlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = TerminationHandler;
    sigaction(SIGTERM, &action, NULL);

    eventLoop = EventLoop_Create();
    if (eventLoop == NULL) {
        Log_Debug("Could not create event loop.\n");
        return ExitCode_Init_EventLoop;
    }


    // Create a UART_Config object, open the UART and set up UART event handler
    UART_Config uartConfig;
    UART_InitConfig(&uartConfig);
    uartConfig.baudRate = 115200;
    uartConfig.flowControl = UART_FlowControl_None;
    uartFd = UART_Open(SAMPLE_UART_LOOPBACK, &uartConfig);
    if (uartFd == -1) {
        Log_Debug("ERROR: Could not open UART: %s (%d).\n", strerror(errno), errno);
        return ExitCode_Init_UartOpen;
    }
    //uartEventReg = EventLoop_RegisterIo(eventLoop, uartFd, EventLoop_Input, UartEventHandler, NULL);
    //if (uartEventReg == NULL) {
    //    return ExitCode_Init_RegisterIo;
    //}

    //// Register a ten second timer to publish setpoint and pv to mqtt broker
    //static const struct timespec sendPeriodUART = { .tv_sec = UART_SEND_PERIOD, .tv_nsec = 0 };
    //sendUartTimer = CreateEventLoopPeriodicTimer(eventLoop, &ButtonTimerEventHandler, &sendPeriodUART);
    //if (sendUartTimer == NULL) {
    //    return ExitCode_Init_Timer;
    //}


    // Open SAMPLE_BUTTON_1 GPIO as input, and set up a timer to poll it
    Log_Debug("Opening SAMPLE_BUTTON_1 as input.\n");
    gpioButtonFd = GPIO_OpenAsInput(SAMPLE_BUTTON_1);
    if (gpioButtonFd == -1) {
        Log_Debug("ERROR: Could not open button GPIO: %s (%d).\n", strerror(errno), errno);
        return ExitCode_Init_OpenButton;
    }
    struct timespec buttonPressCheckPeriod1Ms = {.tv_sec = 0, .tv_nsec = 1000 * 1000};
    buttonPollTimer = CreateEventLoopPeriodicTimer(eventLoop, ButtonTimerEventHandler,
                                                   &buttonPressCheckPeriod1Ms);
    if (buttonPollTimer == NULL) {
        return ExitCode_Init_ButtonPollTimer;
    }

    //// Register a ten second timer to publish setpoint and pv to mqtt broker
    //static const struct timespec sendPeriodMQTT = { .tv_sec = MQTT_PUBLISH_PERIOD, .tv_nsec = 0 };
    //publishMQTTMsgTimer = CreateEventLoopPeriodicTimer(eventLoop, &mqttPublishTimerEventHandler, &sendPeriodMQTT);
    //if (publishMQTTMsgTimer == NULL) {
    //    return ExitCode_Init_Timer;
    //}

    return ExitCode_Success;
}

/// <summary>
///     Closes a file descriptor and prints an error on failure.
/// </summary>
/// <param name="fd">File descriptor to close</param>
/// <param name="fdName">File descriptor name to use in error message</param>
static void CloseFdAndPrintError(int fd, const char *fdName)
{
    if (fd >= 0) {
        int result = close(fd);
        if (result != 0) {
            Log_Debug("ERROR: Could not close fd %s: %s (%d).\n", fdName, strerror(errno), errno);
        }
    }
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
    DisposeEventLoopTimer(buttonPollTimer);
    EventLoop_UnregisterIo(eventLoop, uartEventReg);
    EventLoop_Close(eventLoop);

    Log_Debug("Closing file descriptors.\n");
    CloseFdAndPrintError(gpioButtonFd, "GpioButton");
    CloseFdAndPrintError(uartFd, "Uart");
}





int test_mqtt()
{
    Log_Debug("Im in\n");
    int res = MQTTInit(mqttConf_brokerIp, "1883", mqttConf_topic);
    Log_Debug(res);
    Log_Debug("done init\n");
    MQTTPublish(mqttConf_topic, "{\"engineTemp\":\"67.0\",\"engineRpm\":\"32.0\",\"fuel\":\"51.0\"}");
    while (1)
    {

    }
    //Log_Debug("done publish\n");
}
/// <summary>
///     Main entry point for this application.
/// </summary>
int main(int argc, char *argv[])
{
    Log_Debug("UART application starting.\n");
  //  test_mqtt();
    exitCode = InitPeripheralsAndHandlers();

    // Use event loop to wait for events and trigger handlers, until an error or SIGTERM happens
    while (exitCode == ExitCode_Success) {
        EventLoop_Run_Result result = EventLoop_Run(eventLoop, -1, true);
        // Continue if interrupted by signal, e.g. due to breakpoint being set.
        if (result == EventLoop_Run_Failed && errno != EINTR) {
            exitCode = ExitCode_Main_EventLoopFail;
        }
    }

    ClosePeripheralsAndHandlers();
    Log_Debug("Application exiting.\n");
    return exitCode;
}