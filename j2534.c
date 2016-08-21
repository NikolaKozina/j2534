// requires write permission to device, add udev rule entry such as this to allow:
// SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTR{idProduct}=="cc4d",GROUP="dialout",MODE="0666"

#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>
#include <string.h>

char DllVersion[] = "1.0.2";
char ApiVersion[] = "04.04";
#define LOGFILE "/tmp/op.log"
#define LOGENABLE true

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

void writelog(char* str)
{
#ifdef LOGENABLE
    FILE* logfile;
    logfile=fopen(LOGFILE,"a");
    fprintf(logfile,"%s",str);
    fclose(logfile);
#endif
}
void writelogx(char* str,long unique)
{
#ifdef LOGENABLE
    FILE* logfile;
    logfile=fopen(LOGFILE,"a");
    fprintf(logfile,"[%.8X]%s",unique,str);
    fclose(logfile);
#endif
}
void writelognumber(int num)
{
#ifdef LOGENABLE
    FILE* logfile;
    logfile=fopen(LOGFILE,"a");
    fprintf(logfile,"%d",num);
    fclose(logfile);
#endif
}
void writelogstring(char* str)
{
#ifdef LOGENABLE
    FILE* logfile;
    logfile=fopen(LOGFILE,"a");
    fprintf(logfile,"%s",str);
    fclose(logfile);
#endif
}
void writeloghex(char num)
{
#ifdef LOGENABLE
    FILE* logfile;
    logfile=fopen(LOGFILE,"a");
    fprintf(logfile,"%.8X",num);
    fclose(logfile);
#endif
}
void writeloghexshort(char num)
{
#ifdef LOGENABLE
    FILE* logfile;
    logfile=fopen(LOGFILE,"a");
    fprintf(logfile,"%.2X",(unsigned int)num);
    fclose(logfile);
#endif
}
void writeloghexx(char num,long unique)
{
#ifdef LOGENABLE
    FILE* logfile;
    logfile=fopen(LOGFILE,"a");
    fprintf(logfile,"%.2X[%.8X]",(unsigned)num,unique);
    fclose(logfile);
#endif
}
void writelogpassthrumsg(PASSTHRU_MSG *msg)
{
#ifdef LOGENABLE
    FILE* logfile;
    logfile=fopen(LOGFILE,"a");
    fprintf(logfile,"\tMSG:\n");
    fprintf(logfile,"\t\tProtocolID:\t%d\n",msg->ProtocolID);
    fprintf(logfile,"\t\tRxStatus:\t%.8X\n",msg->RxStatus);
    fprintf(logfile,"\t\tTxFlags:\t%.8X\n",msg->TxFlags);
    fprintf(logfile,"\t\tDataSize:\t%d\n",msg->DataSize);
    int i;
    for (i=0;i<msg->DataSize;i++)
        fprintf(logfile,"\t\t\t%.2X\n",(unsigned int)msg->Data[i]);

    fclose(logfile);
#endif
}

typedef struct ConnectionStruct{
    libusb_device **devs;
    libusb_context *ctx;
    char* VersionString;
    struct libusb_device_handle *dev_handle;
}ConnectionStruct;

ConnectionStruct *con;

int KillSwitch=0;


long PassThruOpen( char* pName, long* pDeviceID)
{
    int r;
    int cnt;
    writelog("Opening...\n\t|\n");
    writelog("\tpName:\t");
    if (pName==NULL)
        writelog("x");
    else
        writelog(pName);
    writelog("\n");

    con=malloc(sizeof(ConnectionStruct));
    con->ctx=NULL;

    r=libusb_init(&con->ctx);
    printf("context: %.8X\n",(int)con->ctx);
	if(r < 0) {
		writelog("Init Error: ");
		writelog(r); //there was an error
	    writelog("\n");
		return 7;
	}
    libusb_set_debug(con->ctx,3);
    con->dev_handle=libusb_open_device_with_vid_pid(con->ctx,0x0403,0xcc4d);
	if(con->dev_handle == NULL) {
		writelog("Cannot open device\n");
		return 8;
	}
	else {
		writelog("Device Opened\n");
		pDeviceID=con->dev_handle;
	}

	//find out if kernel driver is attached
	if(libusb_kernel_driver_active(con->dev_handle, 0) == 1) {
		writelog("Kernel Driver Active\n");
		if(libusb_detach_kernel_driver(con->dev_handle, 0) == 0) //detach it
			writelog("Kernel Driver Detached!\n");
	}
	//claim interface
	r = libusb_claim_interface(con->dev_handle, 0);
	if(r < 0) {
		writelog("Cannot Claim Interface\n");
		return 14;
	}
	writelog("Claimed Interface\n");

    char *data=malloc(sizeof(char)*80);
    data[0]=0x0d;
    data[1]=0x0a;
    data[2]=0x0d;
    data[3]=0x0a;
    data[4]=0x61;
    data[5]=0x74;
    data[6]=0x69;
    data[7]=0x0d;
    data[8]=0x0a;

    int byteswritten;
    r=libusb_bulk_transfer(con->dev_handle,(0x02 | LIBUSB_ENDPOINT_OUT),data,9,&byteswritten,0);
    r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,80,&byteswritten,0);
    data[byteswritten]='\0';
    con->VersionString=malloc(sizeof(char)*byteswritten);
    strcpy(con->VersionString,data);
    //con->VersionString=data

    char *ndata=malloc(sizeof(char)*80);
    writelog("\tMemory corruption 4\n");
    ndata[0]=0x61;
    ndata[1]=0x74;
    ndata[2]=0x61;
    ndata[3]=0x0d;
    ndata[4]=0x0a;

    r=libusb_bulk_transfer(con->dev_handle,(0x02 | LIBUSB_ENDPOINT_OUT),ndata,5,&byteswritten,0);

    char *odata=malloc(sizeof(char)*80);
    r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),odata,80,&byteswritten,0);
    //r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,80,&byteswritten,0);
    writelog("\tMemory corruption 5\n");

    free(data);
    free(ndata);
    writelog("Opened\n");
    return 0;
}


long PassThruClose( long DeviceID )
{
    writelog("Closing...\n\t|\n");
    int r;
    r=libusb_release_interface(con->dev_handle,0);
    libusb_close(con->dev_handle);
    libusb_exit(NULL);
    writelog("Closed\n");
    return r;
}

long PassThruConnect( long DeviceID, long protocolID, long flags, long baud, long* pChannelID )
{
    writelog("Connecting...\n\t|\n");
    writelog("\tDeviceID:\t");
    writeloghex(DeviceID);
    writelog("\n\tprotocolID:\t");
    writeloghex(protocolID);
    writelog("\n\tflags:\t");
    writeloghex(flags);
    writelog("\n\tbaud:\t");
    writeloghex(baud);
    writelog("\n");

    int r;

    //send_connect=libusb_alloc_transfer(0);
    char* data=malloc(sizeof(data)*80);
    strcpy(data,"ato3 ");
    char* str=malloc(sizeof(char)*15);
    snprintf(str,15,"%d",(int)flags);
    strcat(data,str);
    strcat(data," ");
    snprintf(str,15,"%d",(int)baud);
    strcat(data,str);
//    char* spot;
//    spot=strchr(data,'8');

    int strln;
    strln=strlen(data);
    data[strln]=0x0D;
    data[strln+1]=0x0A;
    data[strln+2]=0x00;

    int byteswritten;
    libusb_bulk_transfer(con->dev_handle,(0x02 | LIBUSB_ENDPOINT_OUT),data,strlen(data),&byteswritten,0);
    r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,80,&byteswritten,0);
    //libusb_fill_bulk_transfer(send_connect,con->dev_handle,(0x02 | LIBUSB_ENDPOINT_OUT),data,5,cb_send_connect,NULL,0);
    //r=libusb_submit_transfer(send_connect);
    *pChannelID=3;
    free(data);
    free(str);
    writelog("Connected\n");
    return 0;
}


long PassThruDisconnect( long channelID )
{
    printf("OPEN");
    writelog("PT Disconnect\n");
    return 0;
}

long PassThruReadMsgs( long ChannelID, PASSTHRU_MSG* pMsg, long* pNumMsgs, long Timeout )
{
    writelog("ReadMsgs\n\t|\n");
    writelog("\tChannelID:\t");
    writeloghex(ChannelID);
    writelog("\n\tTimeout:\t");
    writeloghex(Timeout);
    writelog("\n");

    long unique;
    unique=malloc(sizeof(char));
    //printf("Start Read: \n\t|\n");
    //writelogx("\tReadMsg\t",unique);
    int byteswritten;
    long r;
    *pNumMsgs=1;
    PASSTHRU_MSG *msg=malloc(sizeof(PASSTHRU_MSG));
    char* data=malloc(sizeof(char)*8000);
    int i;
    msg->DataSize=0;

    //if (byteswritten>5)

    int dontexit=10;

    //printf("======== %.2X | %.2X | %.2X <> %.2X | %.2X ====\n",data[0],data[1],data[2],data[3],data[4]);
    while (dontexit>0)
    {
        dontexit-=1;
        r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,8000,&byteswritten,Timeout);
        //printf("    %d\n",byteswritten);
        for (i=0;i<byteswritten;i++)
            //printf(" %.2X",data[i]);
        for (i=0;i<byteswritten;i++)
        {
            writelog("\n\t\t");
            writeloghexshort(data[i]);
        }
        writelog("\n");
        //printf("   .\n");
        //writeloghex(data[0]);
        //writelog(" ");
        //writeloghex(data[1]);
        //writelog(" ");
        //writeloghex(data[2]);
        //writelog(" ");
        //writeloghex(data[3]);
        //writelog(" ");
        //writeloghex(data[4]);
        //writelog(" \n");
        if (byteswritten>4)
            printf("======== %.2X | %.2X | %.2X <> %.2X | %.2X ====\n",data[0],data[1],data[2],data[3],data[4]);
        int j;
        for (j=0;j<byteswritten;j++)
            printf(" %.2X",data[j]);
        printf("\n");
                    
        if (data[0]==0x61 && data[1]==0x72 && data[2]==0x33) //ar3
        {
            printf("in if\n");
            dontexit=0;
            switch(data[4])
            {
                case 0xFFFFFFA0:
                    printf("||||||  CASE 1\n");
                    writelog("\t--Case 1\n");
                    msg->RxStatus=0x02;
                    msg->TxFlags=0;
                    memcpy(&msg->Timestamp,data+5,4);
                    msg->DataSize=0;
                    msg->ProtocolID=3;
                    break;
                case 0x80:
                    printf("||||||  CASE 2\n");
                    writelog("\t--Case 2\n");
                    msg->RxStatus=0x02;
                    msg->TxFlags=0;
                    memcpy(&msg->Timestamp,data+5,4);
                    msg->DataSize=0;
                    msg->ProtocolID=3;
                    break;
                case 0xFFFFFF80:
                    printf("||||||  CASE 2\n");
                    writelog("\t--Case 3\n");
                    msg->RxStatus=0x02;
                    msg->TxFlags=0;
                    memcpy(&msg->Timestamp,data+5,4);
                    msg->DataSize=0;
                    msg->ProtocolID=3;
                    break;
                case 0x20:
                    printf("||||||  CASE 3\n");
                    writelog("\t--Case 4\n");
                    msg->ProtocolID=3;
                    msg->RxStatus=0x01;
                    msg->TxFlags=0;
                    msg->DataSize=0;
                    memcpy(msg->Data,data+5,data[3]-1);
                    for(i=0;i<data[3]-1;i++)
                        printf("\t\t %.8X\n",msg->Data[i]);
                    memcpy(&msg->Timestamp,data+10+data[3],4);
                    r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,8000,&byteswritten,Timeout);
                    break;
                case 0x00:
                    printf("||||||  CASE 4:  %d\n",byteswritten);
                    writelog("\t--Case 5\n");
                    msg->RxStatus=0;
                    msg->TxFlags=0;
                    msg->ProtocolID=2;
                    msg->DataSize=0;
                    memcpy(msg->Data,data+5,data[3]);
                    msg->ProtocolID=3;
                    *pMsg=*msg;
                    //printf("Done Read\n\n\n");
                    //return 0;

                    if(data[3]==0x21) {
                        writelog("\t----Double\n");
                        r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,8000,&byteswritten,0);
                        for (i=0;i<byteswritten;i++)
                        {
                            writelog("\n\t\t");
                            writeloghexshort(data[i]);
                        }

                        for(i=0;i<data[3];i++)
                        {
                            msg->Data[i+32]=data[5+i];
                        }
                        msg->DataSize=data[3]+31;
                        printf("datasize+31\n");
                    }else{
                        msg->DataSize=data[3]-1;
                        printf("datasize=data[3]=%d\n",data[3]);
                        memcpy(msg->Data,data+5,msg->DataSize);
                    }
                    break;
                default :
                    printf("Default: %.2X\n", data[4]);
                    msg->ProtocolID=3;
                    dontexit=0;
            }
                    dontexit=0;
                    
        }
        /*
        writelogx("--Discard\n\t",unique);
        */
    }
    //printf("set pmsg  %.8X %d\n",msg,msg->DataSize);
    //*pMsg=*msg;

    if (byteswritten>0)
        *pMsg=*msg;
    else
    {
        msg->ProtocolID=20;
        *pNumMsgs=0;
    }
    //printf("Done Read\n\n\n");
    writelog("\tMsg becomes:\n");
    for(i=0;i<msg->DataSize;i++)
    {
        writelog("\t\t");
        writeloghex(msg->Data[i]);
        writelog("\n");
    }
    writelogx("ReadMsg End\n",unique);
    free(data);
    return 0;
}

long oldPassThruReadMsgs( long ChannelID, PASSTHRU_MSG* pMsg, long* pNumMsgs, long Timeout )
{
    int byteswritten;
    writelog("ReadMsgs\t");
    *pNumMsgs=1;
    PASSTHRU_MSG *msg=malloc(sizeof(PASSTHRU_MSG));
    /*
    *pMsg=*msg;
    msg->RxStatus=0x0002;
    msg->DataSize=0;
    msg->ProtocolID=3;
    *pMsg=*msg;
    return 0;
    */
    msg->ProtocolID=3;
    *pMsg=*msg;
    msg->ProtocolID=3;
    //return 0;
    long r;
    char* data=malloc(sizeof(data)*80);
    printf("attempting to read  |  timeout: %d\n",(int)Timeout);
    r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,80,&byteswritten,Timeout);
    writelognumber(byteswritten);
    writelog("\n");
    printf("BYTES READ: %d\n ",byteswritten);
    int i;
    for (i=0;i<byteswritten;i++)
        printf("    %.8X\n",data[i]);


    if (byteswritten>5)

        printf("==========================  Read: %d === Data: %.2X   %.2X   %.2X   %.2X   %.2X\n",byteswritten,data[0],data[1],data[2],data[3],data[4]);
        if (data[0]==0x61 && data[1]==0x72 && data[2]==0x33 && data[4]==0)
        {
            PASSTHRU_MSG *msg=malloc(sizeof(PASSTHRU_MSG));
            for(i=0;i<data[3];i++)
            {
                printf("Data[%d]  =  data[5+%d] (%.8X)\n",i,i,data[5+i]);
                msg->Data[i]=data[5+i];
            }
            if(data[3]==0x21) {
                printf("DOUBLE LENGTH\n");
                r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,80,&byteswritten,0);

                printf("BYTES READ: %d\n ",byteswritten);
                int i;
                for (i=0;i<byteswritten;i++)
                    printf("    %.8X\n",data[i]);

                printf("WROTE: %d\n",byteswritten);
                printf("data[3]=%d\n",data[3]);
                for(i=0;i<data[3];i++)
                {
                    printf("Data[%d+32]  =  data[3+%d+1] (%.8X)\n",i,i,data[3+i+1]);
                    msg->Data[i+32]=data[5+i];
                }
                msg->DataSize=data[3]+31;
            }else{
                msg->DataSize=data[3];
            }
            *pMsg=*msg;
            msg->ProtocolID=3;

                printf("RxStatus: \n");
                printf("%.8X\n",(unsigned int)msg->RxStatus);
                printf("TxFlags: \n");
                printf("%.8X\n",(unsigned int)msg->TxFlags);
                printf("ExtraDataIndex: \n");
                printf("%.8X\n",(unsigned int)msg->ExtraDataIndex);

                printf("DataSize: %.8X\n", (unsigned int)msg->DataSize);
                for(i=0;i<msg->DataSize;i++)
                {
                    printf("    %.8X\n",msg->Data[i]);
                }
                printf("MESSAGECOUNT: %d", (int)*pNumMsgs);
                printf("TXFLAGS: %.8X\n",(unsigned int)msg->TxFlags);
                printf("RXFLAGS: %.8X\n",(unsigned int)msg->RxStatus);
        }else{
            printf("RXSTATUS\n");
            msg->RxStatus=0x0002;
            msg->DataSize=0;
        }
        writelog("======ProtocolID: ");
        writelognumber(msg->ProtocolID);
        writelog("\n");
    *pMsg=*msg;
    return 0;
}

long PassThruWriteMsgs( long ChannelID, PASSTHRU_MSG* pMsg, long* pNumMsgs, long timeInterval )
{
    int i;
    int r;
    PASSTHRU_MSG *msg;
    msg=pMsg;
    writelog("WriteMsgs\n\t|\n");
    writelog("\tChannelID:\t");
    writeloghex(ChannelID);
    writelog("\n");
    writelogpassthrumsg(pMsg);
    writelog("SEGFAULT1\n");
    writeloghex(msg->Timestamp);
    /*
    writelog("\n\tMSG:  datasize:  ");
    writelognumber(pMsg->DataSize);
    for(i=0;i<pMsg->DataSize;i++)
    {
        writelog("\n\t\t");
        writeloghexshort(pMsg->Data[i]);
    }
    writelog("\n");
    */

    char* data=malloc(sizeof(char)*100);
    char* str=malloc(15000);
    strcpy(data,"att");
    snprintf(str,15,"%d ",(int)ChannelID);
    strcat(data,str);
    snprintf(str,15,"%d 0",(int)msg->DataSize);
    strcat(data,str);
    //writelog(data);
    //writelog("SEGFAULT3\n");
    int strln;
    strln=strlen(data);
    data[strln]=0x0D;
    data[strln+1]=0x0A;
    //writelog("SEGFAULT4\n");
    for(i=0;i<msg->DataSize;i++)
        data[strln+2+i]=msg->Data[i];
    strln=strln+2+msg->DataSize;
    //writelog("SEGFAULT5\n");

    int byteswritten;
    libusb_bulk_transfer(con->dev_handle,(0x02 | LIBUSB_ENDPOINT_OUT),data,strln,&byteswritten,timeInterval);

    writelog("EndWriteMsgs\n");
    return 0;
    free(data);
    free(str);
    return 0;
}

long PassThruStartPeriodicMsg( long ChannelID, char* pMsg, long* pMsgID, long timeInterval )
{
    printf("OPEN");
    writelog("StartPeriodic\n\t|\n");
    return 0;
}

long PassThruStopPeriodicMsg( long ChannelID, long msgID )
{
    printf("OPEN");
    writelog("StopPeriodic\n\t|\n");
    return 0;
}

long PassThruStartMsgFilter( long ChannelID, long FilterType, PASSTHRU_MSG *pMaskMsg, PASSTHRU_MSG *pPatternMsg, PASSTHRU_MSG *pFlowControlMsg, long* pMsgID )
{
    printf("StartFilter");
    writelog("StartMsgFilter\n\t|\n");
    writelog("\tChannelID:\t");
    writeloghex(ChannelID);
    writelog("\n\tFilterType:\t");
    writeloghex(FilterType);
    writelog("\n\tpMaskMsg:\t\n");
    if (pMaskMsg==NULL)
        writelog("x");
    else
    {
        writelogpassthrumsg(pMaskMsg);
    }
    writelog("\n\tpPatternMsg:\t\n");
    if (pPatternMsg==NULL)
        writelog("x");
    else
        writelogpassthrumsg(pPatternMsg);
    writelog("\n\tpFlowControlMsg:\t\n");
    if (pFlowControlMsg==NULL)
        writelog("x");
    else
        writelogpassthrumsg(pFlowControlMsg);
    writelog("\n");


    int r, byteswritten;
    char* data=malloc(sizeof(char)*80);

    strcpy(data,"atf3 1 0 1");
    int datalen;
    datalen=strlen(data);
    data[datalen]=0x0D;
    data[datalen+1]=0x0A;
    data[datalen+2]=0x00;
    data[datalen+3]=0x00;
    data[datalen+4]=0x00;
    r=libusb_bulk_transfer(con->dev_handle,(0x02 | LIBUSB_ENDPOINT_OUT),data,strlen(data)+2,&byteswritten,0);
    r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,80,&byteswritten,0);
    writelog("EndStartMsgFilter\n");
    free(data);
    return 0;
}

long PassThruStopMsgFilter( long ChannelID, long msgID )
{
    printf("StopFilter");
    writelog("StopMsgFilter\n\t|\n");
    writelog("\tChannelID:\t");
    writeloghex(ChannelID);
    writelog("\n\tmsgID:\t\t");
    writeloghex(msgID);
    writelog("\n");
    writelog("EndStopMsgFilter\n");
    return 0;
}

int main(int argc, char** argv)
{
    printf("PassThru Struct Size: %d",sizeof(PASSTHRU_MSG));
    return 0;
    long* pDeviceID;
    PassThruOpen(NULL,pDeviceID);
    PassThruConnect((int)NULL,(int)NULL,0x0200,4800,NULL);
    PassThruStartMsgFilter((int)NULL,(int)NULL,NULL,NULL,NULL,NULL);
    PassThruClose((int)NULL);
    return 0;
}

long PassThruSetProgrammingVoltage( long pinNumber, long voltage )
{
    printf("OPEN");
    writelog("SetProgrammingVoltage not support yet\n");
    return 0;
}

long PassThruReadVersion( long DeviceID, char* pFirmwareVersion, char* pDllVersion, char* pApiVersion )
{
    printf("ReadVersion");
    writelog("ReadVersion\n\t|\n\tfwVer : ");
    strcpy(pFirmwareVersion,con->VersionString);
    strcpy(pDllVersion,DllVersion);
    strcpy(pApiVersion,ApiVersion);
    writelog(pFirmwareVersion);
    writelog("\tlibVer: ");
    writelog(pDllVersion);
    writelog("\n\tapiVer: ");
    writelog(pApiVersion);
    writelog("\n");
    writelog("EndReadVersion\n");
    return 0;
}

long PassThruGetLastError( char* pErrorDescription )
{
    printf("OPEN");
    writelog("GetLastError\n\t|\n");
    writelog("\n\tErrorDescription:\t");
    if (pErrorDescription==NULL)
        writelog("x");
    else
        writelog(pErrorDescription);
    writelog("\n");
    writelog("EndGetLastError\n");
    return 0;
}


long PassThruIoctl( long ChannelID, long ioctlID, SCONFIG_LIST *pInput, char* pOutput)
{
    writelog("Ioctl\n\t|\n");
    writelog("\tChannelID:\t");
    writeloghex(ChannelID);
    writelog("\n\tioctlID:\t");
    writeloghex(ioctlID);
    writelog("\n");
    int r;
    if (ioctlID==2)
    {
        SCONFIG_LIST *inputlist;
        inputlist=pInput;
        int i;
        char* data=malloc(sizeof(char)*80);
        char* str=malloc(sizeof(char)*15);
        for(i=0;i<inputlist->NumOfParams;i++)
        {
            int byteswritten;
            SCONFIG *cfgitem;
            cfgitem=inputlist->ConfigPtr++;
            strcpy(data,"ats3 ");
            writelog("\tConfigItem(p,v):\t");
            writeloghex(cfgitem->Parameter);
            writelog("\t");
            writeloghex(cfgitem->Value);
            writelog("\n");
            snprintf(str,15,"%d ",(int)cfgitem->Parameter);
            strcat(data,str);
            snprintf(str,15,"%d",(int)cfgitem->Value);
            strcat(data,str);
            str[0]=0x0D;
            str[1]=0x0A;
            str[2]=0x00;
            strcat(data,str);
            libusb_bulk_transfer(con->dev_handle,(0x02 | LIBUSB_ENDPOINT_OUT),data,strlen(data),&byteswritten,0);
            r=libusb_bulk_transfer(con->dev_handle,(0x82 | LIBUSB_ENDPOINT_IN),data,80,&byteswritten,0);
        }
        free(data);
        free(str);
    }
    writelog("EndIoctl\n");
    return 0;
}
