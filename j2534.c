/*
 *  USB device requires write permission, add a udev rule entry in /etc/udev/rules.d/
 *  with the contents such as this to allow write access:
 *  SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTR{idProduct}=="cc4d",GROUP="dialout",MODE="0666"
 *  User must be a member of the dialout group on the system
 *
 *  To enable runtime debug logging to the file /tmp/op.log, create an environment variable
 *  with the name LOG_ENABLE and set it to a non-zero value.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libusb.h>
#include <string.h>
#include "byteswap.h"
#include "j2534.h"

const char* DllVersion = "2.0.3";
const char* ApiVersion = "04.04";
const int VENDOR_ID = 0x0403;
const int PRODUCT_ID = 0xCC4D;
int8_t* lastError;
uint8_t A = 0x61;
uint8_t R = 0x72;
uint8_t O = 0x6F;
uint8_t* ARO = "aro\r\n";
ConnectionStruct* con;
PASSTHRU_MSG msgBuf[8];
uint32_t rcvBufIndex = 0;
int littleEndian;
#define DELIMITERS " \r\n"
#define LOGFILE "/tmp/op.log"
int write_log = 0;	//0 = disabled
FILE* logfile;

typedef struct _endpoint {
	uint8_t intf_num;
	uint8_t addr_in;
	uint8_t addr_out;
} Endpoint;

Endpoint* endpoint;
int get_endpoints(libusb_device**, int, const int, const int, Endpoint*);
int isLittleEndian();
uint64_t parse_ts(const void*);
void datacopy(PASSTHRU_MSG*, const int8_t*, int, int, int);

void writelog(int8_t* str)
{
	if (!write_log) return;
	fprintf(logfile, "%s", str);
}

void writelogx(int8_t* str, long unique)
{
	if (!write_log) return;
	fprintf(logfile, "[%08X]%s", (int) unique, str);
}

void writelognumber(int num)
{
	if (!write_log) return;
	fprintf(logfile, "%d", num);
}

void writelogstring(int8_t* str)
{
	if (!write_log) return;
	fprintf(logfile, "%s", str);
}

void writeloghex(int8_t num)
{
	if (!write_log) return;
	fprintf(logfile, "%02X ", (uint8_t) num);
}

void writeloghexshort(int8_t num)
{
	if (!write_log) return;
	fprintf(logfile, "%08X", (uint32_t) num);
}

void writeloghexx(int8_t num, long unique)
{
	if (!write_log) return;
	fprintf(logfile, "%02X[%08X]", (uint8_t) num, (int) unique);
}

void writelogpassthrumsg(const PASSTHRU_MSG* msg)
{
	if (!write_log) return;
	fprintf(logfile, "\tMSG:\n");
	fprintf(logfile, "\t\tProtocolID:\t%d\n", (int32_t)  msg->ProtocolID);
	fprintf(logfile, "\t\tRxStatus:\t%08X\n", (uint32_t) msg->RxStatus);
	fprintf(logfile, "\t\tTxFlags:\t%08X\n",  (uint32_t) msg->TxFlags);
	fprintf(logfile, "\t\tTimeStamp:\t%08X\n",(uint32_t) msg->Timestamp);
	fprintf(logfile, "\t\tDataSize:\t%d\n",   (int32_t)  msg->DataSize);
	fprintf(logfile, "\t\tExtraData:\t%d\n",  (uint32_t) msg->ExtraDataIndex);
	int i;
	fprintf(logfile, "\t\tData:\n\t\t\t");
	for (i = 0; i < msg->DataSize; i++)
		fprintf(logfile, "%02X ", (uint8_t) msg->Data[i]);
	fprintf(logfile, "\n");
}

int isLittleEndian()
{
	/*
	 *  Determine the Endian of the CPU
	 */
	writelog("CPU Endian: ");
	int r = -1;
	uint16_t a = 0x1234;
	if (*((uint8_t* ) &a) == 0x12)
	{
		r = 0;	// false
		writelog("big\n");
	}
	else
	{
		r = 1;	// true
		writelog("little\n");
	}
	return r;
}

uint64_t parse_ts(const void* data)
{
	/*
	 *  This parse_ts function parses four bytes from the given data array
	 *  and copies them to a long, then swaps them based on the processor
	 *  endian.
	 */
	uint64_t timestamp = 0;
	memcpy(&timestamp, data, 4);
	if (littleEndian)
		timestamp = bswap_32(timestamp);
	return timestamp;
}

void datacopy(PASSTHRU_MSG* to, const int8_t* from,
		int to_start, int from_start, int from_end)
{
	/*
	 *  This datacopy function copies bytes between from_start and
	 *  from_end from the object from into the object to beginning at
	 *  to_start
	 */
	int i;
	uint64_t msg_data_idx = to->DataSize;
	writelog("\t\t\t  ");
	for (i = to_start; i < from_end; ++i)
	{
		if (i < (4128 - msg_data_idx))
		{
			to->Data[msg_data_idx + i - to_start] = from[from_start + i];
			writeloghex((uint8_t) from[from_start + i]);
		}
		else
		{
			break;	// to->Data array is full
		}
	}
	writelog("\n");
}

int get_endpoints(libusb_device** devs, int cnt, const int vendor_id,
		const int product_id, Endpoint* endpoint)
{
	/*
	 *  This get_endpoints function determines the addresses for the endpoint
	 *  transmit and receive queues.
	 */
	int x;
	for (x = 0; x < cnt; ++x)
	{
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(devs[x], &desc);
		if (r < 0)
		{
			return r;
		}
		if (desc.idVendor == vendor_id && desc.idProduct == product_id)
		{
			struct libusb_config_descriptor* config;
			r = libusb_get_config_descriptor(devs[x], 0, &config);
			if (r < 0)
			{
				return r;
			}
			const struct libusb_interface* inter;
			const struct libusb_interface_descriptor* interdesc;
			const struct libusb_endpoint_descriptor* epdesc;
			int i;
			for (i = 0; i < config->bNumInterfaces; ++i)
			{
				inter = &config->interface[i];
				int j;
				for (j = 0; j < inter->num_altsetting; ++j)
				{
					interdesc = &inter->altsetting[j];
					if (interdesc->bNumEndpoints == 2)
					{
						int k;
						for (k = 0; k < interdesc->bNumEndpoints; ++k)
						{
							epdesc = &interdesc->endpoint[k];
							if (epdesc->bmAttributes == 2)
							{
								if ((epdesc->bEndpointAddress
										& LIBUSB_ENDPOINT_IN) == 0x80)
								{
									endpoint->addr_in =
											epdesc->bEndpointAddress;
								}
								if ((epdesc->bEndpointAddress
										& LIBUSB_ENDPOINT_IN) == 0x00)
								{
									endpoint->addr_out =
											epdesc->bEndpointAddress;
								}
							}
						}
						endpoint->intf_num = interdesc->bInterfaceNumber;
					}
				}
			}
			libusb_free_config_descriptor(config);
		}
	}
	return 0;
}

long PassThruOpen(const void* pName, unsigned long* pDeviceID)
{
	/*
	 *  Establish a connection with a Pass-Thru device.
	 */
	const char* le = getenv("LOG_ENABLE");
	if (le != NULL)
	{
		if (le[0] == '0')
			write_log = 0;
		else
			write_log = 1;
			logfile = fopen(LOGFILE, "a");

	}
	littleEndian = isLittleEndian();
	writelog("Opening...\n\t|");
	writelog("\n\tpName:\t");
	if (pName == NULL )
		writelog("NULL");
	else
		writelog((int8_t*)pName);
	writelog("\n");

	con = malloc(sizeof(ConnectionStruct));
	con->ctx = NULL;
	endpoint = malloc(sizeof(Endpoint));
	lastError = malloc(sizeof(int8_t) * 80);

	int r;
	r = libusb_init(&con->ctx);
	if (r < 0)
	{
		writelog("\tInit Error: ");
		writelognumber(r); //there was an error
		writelog("\n");
		snprintf(lastError, 80, "%s", "Error initializing USB library\0");
		libusb_exit(con->ctx);
		free(con);
		free(endpoint);
		con = NULL;
		endpoint = NULL;
		return 7;
	}

	libusb_device** devs;
	int cnt = libusb_get_device_list(con->ctx, &devs);
	if (cnt < 0)
	{
		writelog("\tError getting device list\n");
		snprintf(lastError, 80, "%s", "Error getting USB device list\0");
		libusb_exit(con->ctx);
		free(con);
		free(endpoint);
		con = NULL;
		endpoint = NULL;
		return 8;
	}
	r = get_endpoints(devs, cnt, VENDOR_ID, PRODUCT_ID, endpoint);
	libusb_free_device_list(devs, 1);

	con->dev_handle = libusb_open_device_with_vid_pid(con->ctx, VENDOR_ID,
			PRODUCT_ID);
	if (con->dev_handle == NULL )
	{
		writelog("\tCannot open device\n");
		snprintf(lastError, 80, "%s", "Cannot open device (disconnected?)\0");
		libusb_exit(con->ctx);
		free(con);
		free(endpoint);
		con = NULL;
		endpoint = NULL;
		return 8;
	}
	else
	{
		pDeviceID = con->dev_handle;
		writelog("\tDevice Opened, Device ID:");
		writeloghex(*pDeviceID);
		writelog("\n");
	}

	//find out if kernel driver is attached
	if (libusb_kernel_driver_active(con->dev_handle, 0) == 1)
	{
		writelog("\tKernel Driver Active\n");
		if (libusb_detach_kernel_driver(con->dev_handle, 0) == 0) //detach it
			writelog("\tKernel Driver Detached\n");
	}
	//claim interfaces
	r = libusb_claim_interface(con->dev_handle, endpoint->intf_num);
	if (r < 0)
	{
		writelog("\tCannot Claim Interface\n");
		snprintf(lastError, 80, "%s",
				"Cannot claim interface from kernel driver\0");
		libusb_exit(con->ctx);
		free(con);
		free(endpoint);
		con = NULL;
		endpoint = NULL;
		return 14;
	}
	writelog("\tClaimed Interface ");
	writelognumber((int) endpoint->intf_num);
	writelog("\n");

	int8_t* data = malloc(sizeof(int8_t) * 80);
	strcpy(data, "\r\n\r\nati\r\n\0");

	int bytes_written, bytes_read;
	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
			data, strlen(data), &bytes_written, 0);
	do
	{
		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
				data, 80, &bytes_read, 0);
		data[bytes_read] = '\0';
	} while (strcmp(ARO, data) == 0);

	con->VersionString = malloc(sizeof(int8_t) * 80);
	con->VersionString[0] = '\0';
	if (bytes_read > 24
			&& bytes_read < 80
			&& strlen(con->VersionString) == 0)
	{
		int j;
		for (j = 24; j < (bytes_read - 2); ++j)
		{
			con->VersionString[j - 24] = data[j];
		}
		con->VersionString[j - 24] = '\0';
	}
	int8_t* ndata = malloc(sizeof(int8_t) * 80);
	writelog("\tInit sent\n");
	strcpy(ndata, "ata\r\n\0");

	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
			ndata, strlen(ndata), &bytes_written, 0);

	int8_t* odata = malloc(sizeof(int8_t) * 80);
	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
			odata, 80, &bytes_read, 0);
	odata[bytes_read] = '\0';
	if (bytes_read > 24
			&& bytes_read  < 80
			&& strlen(con->VersionString) == 0)
	{
		int j;
		for (j = 24; j < (bytes_read - 2); ++j)
		{
			con->VersionString[j - 24] = odata[j];
		}
		con->VersionString[j - 24] = '\0';
	}
	memset(&msgBuf, 0, sizeof(msgBuf));
	free(data);
	free(ndata);
	free(odata);
	data = NULL;
	ndata = NULL;
	odata = NULL;
	writelog("\tInit acknowledged\nInterface Opened\n");
	snprintf(lastError, 80, "%s", "\0");
	return 0;
}

long PassThruClose(unsigned long DeviceID)
{
	/*
	 *  Terminate a connection with a Pass-Thru device.
	 */
	writelog("Closing...\n\t|\n\tDevice ID: ");
	writeloghex(DeviceID);
	writelog("\n");
	int bytes_written, byte_read, r;
	if (DeviceID != -1) {
		int8_t* data = malloc(sizeof(data) * 80);
		strcpy(data, "atz\r\n\0");

		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
				data, strlen(data), &bytes_written, 0);
		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
				data, 80, &byte_read, 0);
		r = libusb_release_interface(con->dev_handle, endpoint->intf_num);
		libusb_close(con->dev_handle);
		libusb_exit(con->ctx);
		free(data);
		free(con);
		free(endpoint);
		data = NULL;
		con = NULL;
		endpoint = NULL;
	}
	free(lastError);
	lastError = NULL;
	writelog("Closed\n");
	if (write_log) fclose(logfile);
	return 0;
}

long PassThruConnect(unsigned long DeviceID, unsigned long protocolID,
		unsigned long flags, unsigned long baud, unsigned long* pChannelID)
{
	/*
	 *  Establish a connection with a protocol channel.
	 */
	writelog("Connecting...\n\t|");
	writelog("\n\tDeviceID:\t");
	writeloghex(DeviceID);
	writelog("\n\tprotocolID:\t");
	writeloghex(protocolID);
	writelog("\n\tflags:\t");
	writeloghex((int) flags);
	writelog("\n\tbaud:\t");
	writelognumber((int) baud);
	writelog("\n");

	int8_t* data = malloc(sizeof(data) * 80);
	snprintf(data, 80, "ato%d %d %d 0\r\n\0", (int) protocolID, (int) flags,
			(int) baud);

	int bytes_written, bytes_read, r;
	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
			data, strlen(data), &bytes_written, 0);
	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
			data, 80, &bytes_read, 0);
	*pChannelID = protocolID;
	free(data);
	data = NULL;
	writelog("Connected\n");
	return 0;
}

long PassThruDisconnect(unsigned long channelID)
{
	/*
	 *  Terminate a connection with a protocol channel.
	 */
	writelog("Disconnecting ChannelID: ");
	writeloghex(channelID);
	writelog("\n");

	int8_t* data = malloc(sizeof(data) * 80);
	snprintf(data, 80, "atc%d\r\n\0", (int) channelID);

	int bytes_written, bytes_read, r;
	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
			data, strlen(data), &bytes_written, 0);
	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
			data, 80, &bytes_read, 0);
	free(data);
	data = NULL;
	writelog("Disconnected\n");
	return 0;
}

long PassThruWriteMsgs(unsigned long ChannelID, const PASSTHRU_MSG* pMsg,
		unsigned long* pNumMsgs, unsigned long timeInterval)
{
	/*
	 *  Write message(s) to a protocol channel.
	 */
	writelog("WriteMsgs\n\t|");
	writelog("\n\tChannelID:\t");
	writeloghex(ChannelID);
	writelog("\n\tpNumMsgs:\t");
	writelognumber(*pNumMsgs);
	writelog("\n");
	writelogpassthrumsg(pMsg);

	int i, r;
	int8_t* data = malloc(sizeof(int8_t) * 4128);
	int64_t numMsgs = 0;
	for (i = 0; i < *pNumMsgs; ++i)
	{
		snprintf(data, 4128, "att%d %d %d\r\n\0", (int) ChannelID,
				(int) (&pMsg[i])->DataSize, (int) (&pMsg[i])->TxFlags);

		int bytes_written, bytes_read, j, strln;
		strln = strlen(data);
		for (j = 0; j < (&pMsg[i])->DataSize; ++j)
			data[strln++] = (&pMsg[i])->Data[j];

		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
				data, strln, &bytes_written, timeInterval);

		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
				data, 80, &bytes_read, 0);
		data[bytes_read] = '\0';
		if (strcmp(data, ARO) == 0)
			r = 0;
		numMsgs++;
	}
	*pNumMsgs = numMsgs;
	writelog("EndWriteMsgs\n");
	free(data);
	data = NULL;
	return r;
}

long PassThruStartPeriodicMsg(unsigned long ChannelID, const PASSTHRU_MSG* pMsg,
		unsigned long* pMsgID, unsigned long timeInterval)
{
	/*
	 *  Start sending a message at a specified time interval on a protocol channel.
	 */
	writelog("StartPeriodic, not supported\n");
	return 0;
}

long PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long msgID)
{
	/*
	 *  Stop a periodic message.
	 */
	writelog("StopPeriodic, not supported\n");
	return 0;
}

long PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG* pMsg,
		unsigned long* pNumMsgs, unsigned long Timeout)
{
	/*
	 *  Read message(s) from a protocol channel.
	 */
	writelog("ReadMsgs\n\t|");
	writelog("\n\tChannelID:\t");
	writeloghex(ChannelID);
	writelog("\n\tpNumMsgs:\t");
	writelognumber(*pNumMsgs);
	writelog("\n\tTimeout:\t");
	writelognumber(Timeout);
	writelog(" msec");
	writelog("\n\tRCV Buffer:\t");
	writelognumber(rcvBufIndex);
	writelog("\n");
	// RR Logger timeout is 50msec, too short for k-line, let's double it.
	if (Timeout < 100)
		Timeout = Timeout * 2;

	int bytes_read, i, r;
	if (rcvBufIndex < *pNumMsgs)	// if there are not enough msgs in the buffer, read more msgs
	{
		int8_t* log_msg = malloc(sizeof(int8_t) * 128);
		int8_t* data = malloc(sizeof(int8_t) * 4128);
		int8_t* channel = malloc(sizeof(int8_t) * 4);
		snprintf(channel, 4, "%d", (int) ChannelID);

		int dontexit = 10;

		if (rcvBufIndex < 8)
			msgBuf[rcvBufIndex].DataSize = 0;	// Initialize msg datasize
		while (dontexit > 0)
		{
			dontexit--;
			r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
					data, 4128, &bytes_read, Timeout);
			int pos = 5;	// index within "data" array to start a copy from
			int len = 3;	// index of the packet length for the current message in the "data" array
			int bytes_processed = 0;	// the number of bytes processed in the "data" array
			snprintf(log_msg, 128,
					"\t\t*** USB READ: pos:%d, len:%d, bytes_processed:%d, bytes_read:%d, USB:%d\n\t\t",
					pos, len, bytes_processed, bytes_read, r);
			writelog(log_msg);
			for (i = 0; i < bytes_read; ++i)
			{
				writeloghex((uint8_t) data[i]);
			}
			writelog("\n");
			if (bytes_read > 0)
			{
				snprintf(log_msg, 128,
						"\t\t=== %02X | %02X | %02X <> %02X | %02X ===\n",
						(uint8_t) data[0], (uint8_t) data[1],
						(uint8_t) data[2], (uint8_t) data[3],
						(uint8_t) data[4]);
				writelog(log_msg);

				while (bytes_processed < bytes_read)
				{
					if (   data[bytes_processed + 0] == A
						&& data[bytes_processed + 1] == R
						&& data[bytes_processed + 2] == *channel)
					{
						uint8_t channel_id = data[bytes_processed + 2];
						uint8_t packet_type = data[bytes_processed + 4];
						int8_t* msg_type;
						switch (packet_type)
						{
						case (uint8_t) 0xA0:	// Start of a TX LB msg
							msg_type = "TX LB";
							goto SKIP_80;
						case (uint8_t) 0x80:	// Start of a normal msg
							msg_type = "RX Msg";
						SKIP_80:
							msgBuf[rcvBufIndex].Timestamp = parse_ts((data + pos));
							if (channel_id == 0x36)	// CAN message
							{
								datacopy(&msgBuf[rcvBufIndex], data, 4, pos, (data[len] - 1));
								uint64_t dataSize = msgBuf[rcvBufIndex].DataSize + (data[len] - 5);
								msgBuf[rcvBufIndex].DataSize = dataSize;
								msgBuf[rcvBufIndex].ExtraDataIndex = dataSize;
								msgBuf[rcvBufIndex].RxStatus = 9;	// TX Done Loopback
							}
							if (channel_id == 0x33 || channel_id == 0x34)	// K-line message
							{
								msgBuf[rcvBufIndex].DataSize = 0;
								msgBuf[rcvBufIndex].ExtraDataIndex = 0;
								msgBuf[rcvBufIndex].RxStatus = 2;	// Msg start indication
							}
							msgBuf[rcvBufIndex].ProtocolID = ChannelID;
							msgBuf[rcvBufIndex].TxFlags = 0;
							rcvBufIndex++;
							if (rcvBufIndex < 8)
								msgBuf[rcvBufIndex].DataSize = 0;	// Initialize next msg datasize
							bytes_processed = bytes_processed + data[len] + 4;
							pos = bytes_processed + 5;
							len = bytes_processed + 3;
							snprintf(log_msg, 128,
									"\t\t\t--PROCESSED %s INDICATION: pos:%d, len:%d, bytes_processed:%d, bytes_read:%d, ts:%08x, msg_cnt:%d\n",
									msg_type, pos, len, bytes_processed, bytes_read,
									(uint32_t)msgBuf[rcvBufIndex-1].Timestamp, (int)rcvBufIndex);
							writelog(log_msg);
							dontexit = 0;
							break;
						case (uint8_t) 0x20:	// TX LB Msg
							msg_type = "LB Msg";
							msgBuf[rcvBufIndex].RxStatus = 1;	// TX Loopback msg status
							goto SKIP_00;
						case (uint8_t) 0x00:	// normal msg
							msg_type = "RX Msg";
							msgBuf[rcvBufIndex].RxStatus = 0;	// normal msg status
						SKIP_00:;
							uint64_t dataSize = 0;
							if (channel_id == 0x36)	// CAN message
							{
								msgBuf[rcvBufIndex].Timestamp = parse_ts((data + pos));
								datacopy(&msgBuf[rcvBufIndex], data, 4, pos, (data[len] - 1));
								dataSize = msgBuf[rcvBufIndex].DataSize + (data[len] - 5);
								msgBuf[rcvBufIndex].DataSize = dataSize;
								msgBuf[rcvBufIndex].ExtraDataIndex = dataSize;
							}
							if (channel_id == 0x33 || channel_id == 0x34)	// K-line message
							{
								datacopy(&msgBuf[rcvBufIndex], data, 0, pos, (data[len] - 1));
								dataSize = msgBuf[rcvBufIndex].DataSize + (data[len] - 1);
								msgBuf[rcvBufIndex].DataSize = dataSize;
								msgBuf[rcvBufIndex].ExtraDataIndex = dataSize;
							}
							msgBuf[rcvBufIndex].ProtocolID = ChannelID;
							msgBuf[rcvBufIndex].TxFlags = 0;
							bytes_processed = bytes_processed + data[len] + 4;
							pos = bytes_processed + 5;
							len = bytes_processed + 3;
							snprintf(log_msg, 128,
									"\t\t\t--READ %s: pos:%d, len:%d, bytes_processed:%d, bytes_read:%d, DataSize:%d\n",
									msg_type, pos, len, bytes_processed, bytes_read, (int)dataSize);
							writelog(log_msg);
							dontexit++;
							break;
						case (uint8_t) 0x40:	// Msg end indication
							msg_type = "RX Msg";
							goto SKIP_60;
						case (uint8_t) 0x60:	// LB msg end indication
							msg_type = "LB Msg";
						SKIP_60:
							snprintf(log_msg, 128,
									"\t\t\t--%s END INDICATION: pos:%d, len:%d, bytes_processed:%d, bytes_read:%d\n",
									msg_type, pos, len, bytes_processed, bytes_read);
							writelog(log_msg);
							msgBuf[rcvBufIndex].Timestamp = parse_ts((data + pos));
							if (channel_id == 0x36)	// CAN message
							{
								datacopy(&msgBuf[rcvBufIndex], data, 4, pos, (data[len] - 1));
								dataSize = msgBuf[rcvBufIndex].DataSize + (data[len] - 5);
								msgBuf[rcvBufIndex].DataSize = dataSize;
								msgBuf[rcvBufIndex].ExtraDataIndex = dataSize;
								msgBuf[rcvBufIndex].RxStatus = 0;	// RX Indication
							}
							rcvBufIndex++;
							if (rcvBufIndex < 8)
								msgBuf[rcvBufIndex].DataSize = 0;	// Initialize next msg datasize
							bytes_processed = bytes_processed + data[len] + 4;
							pos = bytes_processed + 5;
							len = bytes_processed + 3;
							snprintf(log_msg, 128,
									"\t\t\t--PROCESSED %s END INDICATION: pos:%d, len:%d, bytes_processed:%d, bytes_read:%d, ts:%08x, msg_cnt:%d\n",
									msg_type, pos, len, bytes_processed, bytes_read,
									(uint32_t)msgBuf[rcvBufIndex-1].Timestamp, (int)rcvBufIndex);
							writelog(log_msg);
							dontexit = 0;
							break;
						default:
							snprintf(log_msg, 128,
									"\t\t\t--Unprocessed data length (data[len] = %02X)\n\t\t\t  ",
									data[len]);
							writelog(log_msg);
							for (i = 0; i < (bytes_read - bytes_processed); ++i)
							{
								writeloghex((uint8_t) data[bytes_processed + 5 + i]);
							}
							writelog("\n");
							bytes_processed = bytes_processed + data[len] + 4;
							pos = bytes_processed + 5;
							len = bytes_processed + 3;
							snprintf(log_msg, 128,
									"\t\t\t--DEFAULT: pos:%d, len:%d, bytes_processed:%d, bytes_read:%d, msg_cnt:%d\n",
									pos, len, bytes_processed, bytes_read, (int)rcvBufIndex);
							writelog(log_msg);
							break;
						}	// End of message type switch
						// Check if we have read 8 messages,
						// if so stop reading as pMsg array is full
						if (rcvBufIndex >= 8)
						{
							writelog("Read message array full!");
							goto ARRAY_FULL;
						}
					}	// End of the AR channel# packet

					if (   data[bytes_processed + 0] == A
						&& data[bytes_processed + 1] == R
						&& data[bytes_processed + 2] == O)
					{
						bytes_processed = bytes_processed + 5;
						pos = bytes_processed + 5;
						len = bytes_processed + 3;
						snprintf(log_msg, 128,
								"\t\t\t--ARO Msg: pos:%d, len:%d, bytes_processed:%d, bytes_read:%d, msg_cnt:%d\n",
								pos, len, bytes_processed, bytes_read, (int)rcvBufIndex);
						writelog(log_msg);
					}
				}	// End of bytes to process
			}	// End of bytes read
		}
		ARRAY_FULL:
		for (i = 0; i < rcvBufIndex; ++i)
		{
			writelogpassthrumsg(&msgBuf[i]);
		}
		free(log_msg);
		free(data);
		free(channel);
		log_msg = NULL;
		data = NULL;
		channel = NULL;
	}
	if (rcvBufIndex >= *pNumMsgs)
	{
		// copy the number of requested messages into the provided pointer
		memcpy(pMsg, &msgBuf[0], sizeof(msgBuf[0]) * (*pNumMsgs));
		// calculate the new index position for appending new messages to the RCV buffer
		rcvBufIndex = rcvBufIndex - *pNumMsgs;
		// shift any remaining messages to the beginning of the RCV buffer
		memcpy(&msgBuf[0], &msgBuf[(*pNumMsgs)], (sizeof(msgBuf[0]) * rcvBufIndex));
		// initialize the leftover space in the RCV buffer
		memset(&msgBuf[rcvBufIndex], 0, (sizeof(msgBuf[0]) * (8 - rcvBufIndex)));
	}
	if (rcvBufIndex < 0 || rcvBufIndex > 8)
	{
		rcvBufIndex = 0;
		memset(&msgBuf, 0, sizeof(msgBuf));
		return 0x12;	// ERR_BUFFER_OVERFLOW
	}
	writelog("\tRCV Buffer remaining:\t");
	writelognumber(rcvBufIndex);
	writelog("\nEndReadMsg\n");
	return 0;
}

long PassThruStartMsgFilter(unsigned long ChannelID, unsigned long FilterType,
		const PASSTHRU_MSG* pMaskMsg, const PASSTHRU_MSG* pPatternMsg,
		const PASSTHRU_MSG* pFlowControlMsg, unsigned long* pMsgID)
{
	/*
	 *  Start filtering incoming messages on a protocol channel.
	 */
	writelog("StartMsgFilter\n\t|");
	writelog("\n\tChannelID:\t");
	writeloghex(ChannelID);
	writelog("\n\tFilterType:\t");
	writeloghex(FilterType);
	writelog("\n\tpMaskMsg:\n");
	if (pMaskMsg == NULL )
		writelog("\tNULL");
	else
		writelogpassthrumsg(pMaskMsg);
	writelog("\n\tpPatternMsg:\n");
	if (pPatternMsg == NULL)
		writelog("\tNULL");
	else
		writelogpassthrumsg(pPatternMsg);
	writelog("\n\tpFlowControlMsg:\n");
	if (pFlowControlMsg == NULL)
		writelog("\tNULL");
	else
		writelogpassthrumsg(pFlowControlMsg);
	writelog("\n");

	int8_t* data = malloc(sizeof(int8_t) * 80);
	snprintf(data, 80, "atf%d %d %d %d\r\n\0", (int) ChannelID,
			(int) FilterType, (int) pMaskMsg->TxFlags,
			(int) pMaskMsg->DataSize);

	// append the mask, pattern and flow control bytes keeping track of the final byte count
	int bytes_written, bytes_read, r, datalen, i, j = 0;
	datalen = strlen(data);
	for (i = datalen; i < (datalen + pMaskMsg->DataSize); ++i)
	{
		data[i] = pMaskMsg->Data[j];
		j++;
	}
	datalen = i;
	j = 0;
	for (i = datalen; i < (datalen + pPatternMsg->DataSize); ++i)
	{
		data[i] = pPatternMsg->Data[j];
		j++;
	}
	if (pFlowControlMsg != NULL )
	{
		datalen = i;
		j = 0;
		for (i = datalen; i < (datalen + pFlowControlMsg->DataSize); ++i)
		{
			data[i] = pFlowControlMsg->Data[j];
			j++;
		}
	}

	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
			data, i, &bytes_written, 0);
	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
			data, 80, &bytes_read, 0);
	data[bytes_read] = '\0';
	int8_t* word = strtok(data, DELIMITERS);
	word = strtok(NULL, DELIMITERS);
	*pMsgID = atol(word);

	free(data);
	data = NULL;
	writelog("EndStartMsgFilter\n");
	return 0;
}

long PassThruStopMsgFilter(unsigned long ChannelID, unsigned long msgID)
{
	/*
	 *  Stops filtering incoming messages on a protocol channel.
	 */
	writelog("StopMsgFilter\n\t|\n\tChannelID:\t");
	writeloghex(ChannelID);
	writelog("\n\tmsgID:\t\t");
	writeloghex(msgID);
	writelog("\n");

	int8_t* data = malloc(sizeof(data) * 80);
	snprintf(data, 80, "atk%d %d\r\n\0", (int) ChannelID, (int) msgID);

	int bytes_written, bytes_read, r;
	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
			data, strlen(data), &bytes_written, 0);
	r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
			data, 80, &bytes_read, 0);
	free(data);
	data = NULL;
	writelog("EndStopMsgFilter\n");
	return 0;
}

long PassThruSetProgrammingVoltage(unsigned long DeviceID,
		unsigned long pinNumber, unsigned long voltage)
{
	/*
	 *  Set a programming voltage on a specific pin.
	 */
	writelog("SetProgrammingVoltage, not support\n");
	return 0;
}

long PassThruReadVersion(unsigned long DeviceID, char* pFirmwareVersion,
		char* pDllVersion, char* pApiVersion)
{
	/*
	 *  Reads the version information for the DLL and API.
	 */
	strcpy(pFirmwareVersion, con->VersionString);
	strcpy(pDllVersion, DllVersion);
	strcpy(pApiVersion, ApiVersion);
	writelog("ReadVersion\n\t|\n\tfwVer : ");
	writelog(pFirmwareVersion);
	writelog("\n\tlibVer: ");
	writelog(pDllVersion);
	writelog("\n\tapiVer: ");
	writelog(pApiVersion);
	writelog("\n");
	writelog("EndReadVersion\n");
	return 0;
}

long PassThruGetLastError(char* pErrorDescription)
{
	/*
	 *  Gets the text description of the last error.
	 */
	writelog("GetLastError\n\t|");
	writelog("\n\tErrorDescription:\t");
	pErrorDescription = lastError;
	if (pErrorDescription == NULL )
	{
		writelog("NULL");
		return 4;
	} else
		writelog(pErrorDescription);
	writelog("\n");
	writelog("EndGetLastError\n");
	return 0;
}

long PassThruIoctl(unsigned long ChannelID, unsigned long ioctlID,
		const void* pInput, void* pOutput)
{
	/*
	 *  General I/O control functions for reading and writing
	 *  protocol configuration parameters (e.g. initialization,
	 *  baud rates, programming voltages, etc.).
	 */
	writelog("Ioctl\n\t|\n\tChannelID:\t");
	writeloghex(ChannelID);
	writelog("\n\tioctlID:\t");
	writeloghex(ioctlID);
	int8_t* data = malloc(sizeof(int8_t) * 80);
	int bytes_written, bytes_read, r, i;
	r = 1;
	if (ioctlID == 1)
	{
		const SCONFIG_LIST* inputlist = pInput;
		writelog(" [Config GET]\n\tNumOfParams: ");
		writelognumber(inputlist->NumOfParams);
		writelog("\n");
		SCONFIG* cfgitem;
		for (i = 0; i < inputlist->NumOfParams; ++i)
		{
			cfgitem = &inputlist->ConfigPtr[i];
			snprintf(data, 80, "atg%d %d\r\n\0", (int) ChannelID,
					(int) cfgitem->Parameter);
			r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
					data, strlen(data), &bytes_written, 0);
			r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
					data, 80, &bytes_read, 0);
			data[bytes_read] = '\0';

			int8_t* word = strtok(data, DELIMITERS);
			word = strtok(NULL, DELIMITERS);
			if (cfgitem->Parameter == atol(word))
			{
				word = strtok(NULL, DELIMITERS);
				cfgitem->Value = atol(word);
			}

			writelog("\t\tConfigItem(p,v):\t");
			writeloghex(cfgitem->Parameter);
			writelog("\t");
			writeloghex(cfgitem->Value);
			writelog("\n");
		}
	}
	if (ioctlID == 2)
	{
		const SCONFIG_LIST* inputlist = pInput;
		writelog(" [Config SET]\n\tNumOfParams: ");
		writelognumber(inputlist->NumOfParams);
		writelog("\n");
		SCONFIG* cfgitem;
		for (i = 0; i < inputlist->NumOfParams; ++i)
		{
			cfgitem = &inputlist->ConfigPtr[i];
			snprintf(data, 80, "ats%d %d %d\r\n\0", (int) ChannelID,
					(int) cfgitem->Parameter, (int) cfgitem->Value);
			writelog("\t\tConfigItem(p,v):\t");
			writeloghex(cfgitem->Parameter);
			writelog("\t");
			writeloghex(cfgitem->Value);
			writelog("\n");
			r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
					data, strlen(data), &bytes_written, 0);
			r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
					data, 80, &bytes_read, 500);
		}
	}
	if (ioctlID == 3)
	{
		writelog(" [READ_VBATT]\n");
		long* vBatt = pOutput;
		long pin = 16;
		snprintf(data, 80, "atr %d\r\n\0", (int) pin);
		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
				data, strlen(data), &bytes_written, 0);
		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
				data, 80, &bytes_read, 0);
		data[bytes_read] = '\0';

		int8_t* word = strtok(data, DELIMITERS);
		word = strtok(NULL, DELIMITERS);
		if (pin == atol(word))
		{
			word = strtok(NULL, DELIMITERS);
			*vBatt = atol(word);
		}
		writelog("\t\tPin 16 Voltage:\t");
		writelognumber((int)*vBatt);
		writelog("mV\n");
	}
	if (ioctlID == 5)
	{
		const PASSTHRU_MSG* pMsg = pInput;
		writelog(" [FAST INIT]\n");
		writelogpassthrumsg(pMsg);
		snprintf(data, 80, "aty%d %d 0\r\n\0", (int) ChannelID,
				(int) pMsg->DataSize);
		for (i = 0; i < (int) pMsg->DataSize; ++i)
		{
			data[10 + i] = pMsg->Data[i];
		}
		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
				data, strlen(data), &bytes_written, 0);
		if (r < 0)
			goto EXIT_IOCTL;
		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
				data, 80, &bytes_read, 500);
		if (r < 0)
			goto EXIT_IOCTL;
		uint64_t len = (uint64_t) atol(data + 5);
		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
				data, 80, &bytes_read, 500);
		if (r < 0)
			goto EXIT_IOCTL;
		PASSTHRU_MSG* pOutMsg = pOutput;
		pOutMsg->DataSize = 0;
		datacopy(pOutMsg, data,	0, 0, len);
		pOutMsg->DataSize = len;
		pOutMsg->ExtraDataIndex = len;
		pOutMsg->RxStatus = 0;
		pOutMsg->ProtocolID = ChannelID;
		writelogpassthrumsg(pOutMsg);
	}
	if (ioctlID == 7)
	{
		writelog(" [CLEAR_TX_BUFFER]\n");
		r = 0;
	}
	if (ioctlID == 8)
	{
		memset(&msgBuf, 0, sizeof(msgBuf));
		uint32_t rcvBufIndex = 0;
		writelog(" [CLEAR_RX_BUFFER]\n");
		r = 0;
	}
	EXIT_IOCTL:
	free(data);
	data = NULL;
	writelog("EndIoctl\n");
	return r;
}
