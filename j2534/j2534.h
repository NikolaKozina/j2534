/*
  Copyright (C) 2022
  Authors: NikolaKozina
            Dale Schultz

  You are free to use this software for any purpose, but please keep
  acknowledge where it came from!

  On Windows, this shared library is compiled with the OP2J2534_EXPORTS
  symbol defined on the command line. This symbol should not be defined on any project
  that uses this DLL.  This way any other project whose source files include this file see
  OP2J2534_API functions as being imported from a DLL, whereas this DLL sees symbols
  defined with this macro as being exported.

 */

#ifndef OP2J2534_H
    #define OP2J2534_H

const char *DLL_VERSION = "3.0.0";
const char *API_VERSION = "04.04";

#define PM_DATA_LEN	4128 // Maixmum length of data in a PASSTHRU_MSG

#ifdef _MSC_VER
  #ifdef OP2J2534_EXPORTS
    #define OP2J2534_API __declspec(dllexport)
  #else
    #define OP2J2534_API __declspec(dllimport)
  #endif
#else
  #define OP2J2534_API
#endif

#ifndef FALSE
    #define FALSE 0
#endif
#ifndef TRUE
    #define TRUE 1
#endif

#ifdef __cplusplus
extern "C" {  // only need to export C interface if
              // used by C++ source code
#endif

#include <stdint.h>

enum j2534_error {
    J2534_NOERROR,
    J2534_ERR_NOT_SUPPORTED,
    J2534_ERR_INVALID_CHANNEL_ID,
    J2534_ERR_INVALID_PROTOCOL_ID,
    J2534_ERR_NULL_PARAMETER,
    J2534_ERR_INVALID_IOCTL_VALUE,
    J2534_ERR_INVALID_FLAGS,
    J2534_ERR_FAILED,
    J2534_ERR_DEVICE_NOT_CONNECTED,
    J2534_ERR_TIMEOUT,
    J2534_ERR_INVALID_MSG,
    J2534_ERR_INVALID_TIME_INTERVAL,
    J2534_ERR_EXCEEDED_LIMIT,
    J2534_ERR_INVALID_MSG_ID,
    J2534_ERR_DEVICE_IN_USE,
    J2534_ERR_INVALID_IOCTL_ID,
    J2534_ERR_BUFFER_EMPTY,
    J2534_ERR_BUFFER_FULL,
    J2534_ERR_BUFFER_OVERFLOW,
    J2534_ERR_PIN_INVALID,
    J2534_ERR_CHANNEL_IN_USE,
    J2534_ERR_MSG_PROTOCOL_ID,
    J2534_ERR_INVALID_FILTER_ID,
    J2534_ERR_NO_FLOW_CONTROL,
    J2534_ERR_NOT_UNIQUE,
    J2534_ERR_INVALID_BAUDRATE,
    J2534_ERR_INVALID_DEVICE_ID
};

enum j2534_ioctl {
    J2534_GET_CONFIG = 1,
    J2534_SET_CONFIG,
    J2534_READ_VBATT,
    J2534_FIVE_BAUD_INIT,
    J2534_FAST_INIT,
    J2534_CLEAR_TX_BUFFER = 7,
    J2534_CLEAR_RX_BUFFER,
    J2534_CLEAR_PERIODIC_MSGS,
    J2534_CLEAR_MSG_FILTERS,
    J2534_CLEAR_FUNCT_MSG_LOOKUP_TABLE,
    J2534_ADD_TO_FUNCT_MSG_LOOKUP_TABLE,
    J2534_DELETE_FROM_FUNCT_MSG_LOOUP_TABLE,
    J2534_READ_PROG_VOLTAGE
};

enum j2534_filter {
    J2534_PASS_FILTER = 1,
    J2534_BLOCK_FILTER,
    J2534_FLOW_CONTROL_FILTER
};

typedef struct _SCONFIG
{
    unsigned long Parameter;
    unsigned long Value;
} SCONFIG;

typedef struct _SCONFIG_LIST
{
    unsigned long NumOfParams;
    SCONFIG *ConfigPtr;
} SCONFIG_LIST;

typedef struct _PASSTHRU_MSG
{
    unsigned long ProtocolID;
    unsigned long RxStatus;
    unsigned long TxFlags;
    unsigned long Timestamp;
    unsigned long DataSize;
    unsigned long ExtraDataIndex;
    unsigned char Data[PM_DATA_LEN];
} PASSTHRU_MSG;

OP2J2534_API int32_t PassThruOpen(
    const void *pName, unsigned long *pDeviceID);
OP2J2534_API int32_t PassThruClose(
    const unsigned long DeviceID);
OP2J2534_API int32_t PassThruConnect(
    const unsigned long DeviceID, const unsigned long ProtocolID, const unsigned long Flags,
    const unsigned long Baudrate, unsigned long *pChannelID);
OP2J2534_API int32_t PassThruDisconnect(
    const unsigned long ChannelID);
OP2J2534_API int32_t PassThruReadMsgs(
    const unsigned long ChannelID, PASSTHRU_MSG *pMsg,
    unsigned long *pNumMsgs, const unsigned long Timeout);
OP2J2534_API int32_t PassThruWriteMsgs(
    const unsigned long ChannelID, const PASSTHRU_MSG *pMsg,
    unsigned long *pNumMsgs, const unsigned long Timeout);
OP2J2534_API int32_t PassThruStartPeriodicMsg(
    const unsigned long ChannelID, const PASSTHRU_MSG *pMsg,
    const unsigned long *pMsgID, const unsigned long TimeInterval);
OP2J2534_API int32_t PassThruStopPeriodicMsg(
    const unsigned long ChannelID, const unsigned long MsgID);
OP2J2534_API int32_t PassThruStartMsgFilter(
    const unsigned long ChannelID, unsigned long FilterType,
    const PASSTHRU_MSG *pMaskMsg, const PASSTHRU_MSG *pPatternMsg,
    const PASSTHRU_MSG *pFlowControlMsg, unsigned long *pMsgID);
OP2J2534_API int32_t PassThruStopMsgFilter(
    const unsigned long ChannelID, const unsigned long MsgID);
OP2J2534_API int32_t PassThruSetProgrammingVoltage(
    const unsigned long DeviceID, const unsigned long Pin, const unsigned long Voltage);
OP2J2534_API int32_t PassThruReadVersion(
    const unsigned long DeviceID, char *pApiVersion, char *pDllVersion, char *pFirmwareVersion);
OP2J2534_API int32_t PassThruGetLastError(
    char *pErrorDescription);
OP2J2534_API int32_t PassThruIoctl(
    const unsigned long ChannelID, const unsigned long IoctlID, const void *pInput, void *pOutput);

#ifdef __cplusplus
}
#endif
#endif  // OP2J2534_H
