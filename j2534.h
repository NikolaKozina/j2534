#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>
#include <string.h>

typedef struct _SCONFIG
{
    unsigned long Parameter;
    unsigned long Value;
} SCONFIG;

typedef struct _SCONFIG_LIST
{
    unsigned long NumOfParams;
    SCONFIG* ConfigPtr;
} SCONFIG_LIST;

typedef struct _PASSTHRU_MSG
{
    unsigned long ProtocolID;
    unsigned long RxStatus;
    unsigned long TxFlags;
    unsigned long Timestamp;
    unsigned long DataSize;
    unsigned long ExtraDataIndex;
    unsigned char Data[4128];
} PASSTHRU_MSG;

typedef struct _ConnectionStruct
{
    libusb_device** devs;
    libusb_context* ctx;
    char* VersionString;
    struct libusb_device_handle* dev_handle;
} ConnectionStruct;

long PassThruOpen(const void* pName, unsigned long* pDeviceID);
long PassThruClose(unsigned long DeviceID);
long PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID, unsigned long Flags,
					unsigned long Baudrate, unsigned long* pChannelID);
long PassThruDisconnect(unsigned long ChannelID);
long PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG* pMsg, unsigned long* pNumMsgs, unsigned long Timeout);
long PassThruWriteMsgs(unsigned long ChannelID, const PASSTHRU_MSG* pMsg, unsigned long* pNumMsgs, unsigned long Timeout);
long PassThruStartPeriodicMsg(unsigned long ChannelID, const PASSTHRU_MSG* pMsg, unsigned long* pMsgID, unsigned long TimeInterval);
long PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID);
long PassThruStartMsgFilter(unsigned long ChannelID,
                      unsigned long FilterType, const PASSTHRU_MSG* pMaskMsg, const PASSTHRU_MSG* pPatternMsg,
                      const PASSTHRU_MSG* pFlowControlMsg, unsigned long* pMsgID);
long PassThruStopMsgFilter(unsigned long ChannelID, unsigned long MsgID);
long PassThruSetProgrammingVoltage(unsigned long DeviceID, unsigned long Pin, unsigned long Voltage);
long PassThruReadVersion(unsigned long DeviceID, char* pApiVersion, char* pDllVersion, char* pFirmwareVersion);
long PassThruGetLastError(char* pErrorDescription);
long PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID, const void* pInput, void* pOutput);
