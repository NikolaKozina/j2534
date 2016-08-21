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
    unsigned char Data[4128];
} PASSTHRU_MSG;

typedef struct ConnectionStruct{
    libusb_device **devs;
    libusb_context *ctx;
    char* VersionString;
    struct libusb_device_handle *dev_handle;
}ConnectionStruct;

long PassThruOpen( char* pName, long* pDeviceID );
long PassThruClose( long DeviceID );
long PassThruConnect( long DeviceID, long protocolID, long flags, long baud, long* pChannelID );
long PassThruDisconnect( long channelID );
long PassThruReadMsgs( long ChannelID, PASSTHRU_MSG* pMsg, long* pNumMsgs, long Timeout );
long PassThruWriteMsgs( long ChannelID, PASSTHRU_MSG* pMsg, long* pNumMsgs, long timeInterval );
long PassThruStartPeriodicMsg( long ChannelID, char* pMsg, long* pMsgID, long timeInterval );
long PassThruStopPeriodicMsg( long ChannelID, long msgID );
long PassThruStartMsgFilter( long ChannelID, long FilterType, PASSTHRU_MSG *pMaskMsg, PASSTHRU_MSG *pPatternMsg, PASSTHRU_MSG *pFlowControlMsg, long* pMsgID );
long PassThruStopMsgFilter( long ChannelID, long msgID );
long PassThruSetProgrammingVoltage( long DeviceID, long pinNumber, long voltage );
long PassThruReadVersion( char* pFirmwareVersion, char* pDllVersion, char* pApiVersion );
long PassThruGetLastError( char* pErrorDescription );
long PassThruIoctl( long ChannelID, long ioctlID, SCONFIG_LIST *pInput, char* pOutput);
