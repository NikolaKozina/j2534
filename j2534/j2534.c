/*
  Copyright (C) 2022
  Authors: NikolaKozina
			Dale Schultz

  You are free to use this software for any purpose, but please keep
  acknowledge where it came from!

  Before using this library, remove the SD card from the Openport 2.0

  On Linux, USB device requires write permission, add a udev rule entry in /etc/udev/rules.d/
  with the contents such as this to allow write access:
  SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTR{idProduct}=="cc4d", GROUP="dialout", MODE="0666"
  User must be a member of the dialout group on the system

  On Windows, the Openport 2.0 system driver has to be changed from the one provided by
  Tactrix to WinUSB.  The easiest way to complete this is to use Zadig (https://zadig.akeo.ie/).

  To enable runtime debug logging to a file, create an environment variable
  with the name LOG_ENABLE and set it to the path and filename to write to.
  To enable libusb messages, create a LIBUSB_DEBUG environment variable and set libusb
  message verbosity:
	NONE = 0, ERROR = 1, WARNING = 2, INFO = 3, DEBUG = 4

  If linked with libusb version 1.0.10 thru 1.0.12, define a preprocessor symbol LIBUSB1010  before
  compilation to enable libusb library version reporting in this library's version info string.
 */

#include "j2534.h"
#include <errno.h>
#include <libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) { return TRUE; }
#endif

// LIBUSBX_API_VERSION is available in libusb version 1.0.13 and later
#if defined(LIBUSB_API_VERSION) || defined(LIBUSBX_API_VERSION) || defined(LIBUSB1010)
#define GET_LIBUSB_VERSION
#endif

#define MAX_LEN	80	// Maximum length of small data message
#define LE_LEN	80	// Maximum length of an error message string
#define LM_LEN 256	// Maximum length of writelog() message

typedef struct _connection
{
	uint8_t device_id;
	int8_t  channel;
	unsigned long protocol_id;
	struct libusb_context *ctx;
	struct libusb_device_handle *dev_handle;
} connection_t;

typedef struct _endpoint
{
	uint8_t intf_num;
	uint8_t addr_in;
	uint8_t addr_out;
} endpoint_t;

typedef struct _fifo_msg
{
	PASSTHRU_MSG *pt_msg;
	struct _fifo_msg *next_in_q;
} fifo_msg_t;

const char *DELIMITERS = " \r\n";
const uint16_t VENDOR_ID = 0x0403;
const uint16_t PRODUCT_ID = 0xcc4d;
const uint8_t ISO9141 = 0x33;
const uint8_t ISO14230 = 0x34;
const uint8_t CAN = 0x35;
const uint8_t ISO15765 = 0x36;
int8_t LAST_ERROR[LE_LEN];
int littleEndian = TRUE;
int write_log = FALSE;
int8_t log_msg[LM_LEN];
unsigned long rx_buf_idx = 0;
char fw_version[MAX_LEN];
FILE *logfile;
connection_t con[1];
endpoint_t endpoint[1];
fifo_msg_t *fifo_head = NULL;

enum rx_msg_type {
	NORM_MSG,
	TX_DONE = 0x10,
	TX_LB_MSG = 0x20,
	RX_MSG_END_IND = 0x40,
	EXT_ADDR_MSG_END_IND = 0x44,
	LB_MSG_END_IND = 0x60,
	NORM_MSG_START_IND = 0x80,
	TX_LB_START_IND = 0xA0,
};

static void writelog(const char *str)
{
	fprintf(logfile, "%s", str);
}

static void writeloghex(const int8_t num)
{
	fprintf(logfile, "%02X ", (uint8_t)num);
}

static void writelogmsg(const uint8_t *data, const unsigned long start, const unsigned long len)
{
	unsigned long i = start;
	for (; i < len; i++)
		fprintf(logfile, "%02X ", (uint8_t)data[i]);
}

static void writelogpassthrumsg(const PASSTHRU_MSG *msg)
{
	fprintf(logfile,
		"\tMSG: %p\n"
		"\t\tProtocolID:\t%lu\n"
		"\t\tRxStatus:\t%08lX\n"
		"\t\tTxFlags:\t%08lX\n"
		"\t\tTimeStamp:\t0x%08lX (%lu \xC2\xB5sec)\n" // micro seconds
		"\t\tDataSize:\t%lu\n"
		"\t\tExtraData:\t%lu\n"
		"\t\tData:\n\t\t\t",
		msg, msg->ProtocolID, msg->RxStatus, msg->TxFlags, msg->Timestamp,
		msg->Timestamp, msg->DataSize, msg->ExtraDataIndex);
	writelogmsg(msg->Data, 0, msg->DataSize);
	fprintf(logfile, "\n");
}

/*
   Determine the Endian of the CPU
 */
static int isLittleEndian()
{
	int r = -1;
	uint16_t a = 0x1234;
	if (*((uint8_t*)&a) == 0x12)
	{
		r = FALSE;
		if (write_log)
			writelog("CPU Endian: big\n");
	}
	else
	{
		r = TRUE;
		if (write_log)
			writelog("CPU Endian: little\n");
	}
	return r;
}

/*
   Byte swap and return the provided 4-byte value
 */
static uint32_t bswap(const uint32_t bytes)
{
	union {
		uint8_t  B0[4];
		uint32_t B4;
	} tmp_bytes;
	tmp_bytes.B0[0] = (uint8_t)(bytes >> 24);
	tmp_bytes.B0[1] = (uint8_t)(bytes >> 16);
	tmp_bytes.B0[2] = (uint8_t)(bytes >> 8);
	tmp_bytes.B0[3] = (uint8_t)(bytes & 0xff);
	return tmp_bytes.B4;
}

/*
  Parse a timestamp. This parse_ts function parses four bytes from
  the given data array and copies them to a long, then swaps them
  based on the processor endian.
 */
static uint32_t parse_ts(const void *data)
{
	uint32_t timestamp = 0;
	memcpy(&timestamp, data, 4);
	if (littleEndian)
		timestamp = bswap(timestamp);
	return timestamp;
}

/*
  This copy function copies bytes, 2 at a time between s_start to s_end
  from the object src into the object dest beginning at d_pos.
  */
static void datacopy(PASSTHRU_MSG *dest, const int8_t *src,
	const uint32_t d_pos, const uint32_t s_start, const uint32_t s_end)
{
	uint32_t i = d_pos;
	uint32_t dest_idx = dest->DataSize;
	uint32_t idx = dest_idx - d_pos;
	uint32_t limit = PM_DATA_LEN - dest_idx;
	uint32_t x2_end = s_end - 1;
	uint32_t x2_limit = limit - 1;

	if (write_log) writelog("\t\t\t  ");

	// Copy 2 bytes per iteration
	// check if at x2_end of src or dest->Data is almost full
	for (; (i < x2_end) && (i < x2_limit); i += 2)
	{
		dest->Data[idx + i] = src[s_start + i];
		dest->Data[idx + i + 1] = src[s_start + i + 1];
		if (write_log)
		{
			writeloghex(src[s_start + i]);
			writeloghex(src[s_start + i + 1]);
		}
	}
	// Copy last byte if any
	// check end of src or dest->Data is full
	for (; (i < s_end) && (i < limit); i++)
	{
		dest->Data[idx + i] = src[s_start + i];
		if (write_log) writeloghex(src[s_start + i]);
	}
	if (write_log) writelog("\n");
}

/*
   This open_dev_endpoints function locates the device to open by Vendor and
   Product.  Opens the device and sets the handle to use, then determines the
   addresses for the endpoint transmit and receive queues.
 */
static int open_dev_endpoints(libusb_device **devs, const ssize_t cnt,
	const uint16_t vendor_id, const uint16_t product_id, endpoint_t *endpoint)
{
	ssize_t x = 0;
	for (; x < cnt; x++)
	{
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(devs[x], &desc);
		if (r != LIBUSB_SUCCESS)
			return r;

		if (desc.idVendor == vendor_id && desc.idProduct == product_id)
		{
			r = libusb_open(devs[x], &con->dev_handle);
			if (r != LIBUSB_SUCCESS)
				return r;

			con->device_id = libusb_get_device_address(devs[x]);
			struct libusb_config_descriptor *config;
			r = libusb_get_config_descriptor(devs[x], 0, &config);
			if (r != LIBUSB_SUCCESS)
				return r;

			const struct libusb_interface *inter;
			const struct libusb_interface_descriptor *interdesc;
			const struct libusb_endpoint_descriptor *epdesc;
			uint8_t i = 0;
			for (; i < config->bNumInterfaces; i++)
			{
				inter = &config->interface[i];
				int j = 0;
				for (; j < inter->num_altsetting; j++)
				{
					interdesc = &inter->altsetting[j];
					if (interdesc->bNumEndpoints == 2)
					{
						uint8_t k = 0;
						for (; k < interdesc->bNumEndpoints; k++)
						{
							epdesc = &interdesc->endpoint[k];
							if ((epdesc->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK)
							{
								if ((epdesc->bEndpointAddress & LIBUSB_ENDPOINT_IN) == LIBUSB_ENDPOINT_IN)
									endpoint->addr_in = epdesc->bEndpointAddress;
								if ((epdesc->bEndpointAddress & LIBUSB_ENDPOINT_IN) == LIBUSB_ENDPOINT_OUT)
									endpoint->addr_out = epdesc->bEndpointAddress;
							}
						}
						endpoint->intf_num = interdesc->bInterfaceNumber;
					}
				}
			}
			libusb_free_config_descriptor(config);
			return LIBUSB_SUCCESS;
		}
	}
	return LIBUSB_ERROR_NO_DEVICE;
}

/*
  Error map, convert a libusb error code value to a PassThru error code.
*/
static int error_map(const int usb_err)
{
	switch (usb_err) {
	case LIBUSB_SUCCESS:
		return J2534_NOERROR;
	case LIBUSB_ERROR_IO:
		return J2534_ERR_DEVICE_NOT_CONNECTED;
	case LIBUSB_ERROR_INVALID_PARAM:
		return J2534_ERR_FAILED;
	case LIBUSB_ERROR_ACCESS:
		return J2534_ERR_DEVICE_IN_USE;
	case LIBUSB_ERROR_NO_DEVICE:
		return J2534_ERR_DEVICE_NOT_CONNECTED;
	case LIBUSB_ERROR_NOT_FOUND:
		return J2534_ERR_DEVICE_NOT_CONNECTED;
	case LIBUSB_ERROR_BUSY:
		return J2534_ERR_DEVICE_IN_USE;
	case LIBUSB_ERROR_TIMEOUT:
		return J2534_ERR_TIMEOUT;
	case LIBUSB_ERROR_OVERFLOW:
		return J2534_ERR_BUFFER_OVERFLOW;
	case LIBUSB_ERROR_PIPE:
		return J2534_ERR_FAILED;
	case LIBUSB_ERROR_INTERRUPTED:
		return J2534_ERR_FAILED;
	case LIBUSB_ERROR_NO_MEM:
		return J2534_ERR_EXCEEDED_LIMIT;
	case LIBUSB_ERROR_NOT_SUPPORTED:
		return J2534_ERR_NOT_SUPPORTED;
	case LIBUSB_ERROR_OTHER:
		return J2534_ERR_FAILED;
	default:
		return usb_err;
	}
}

/*
	Check if characters converted correctly to a number
*/
static int is_valid(const unsigned long value)
{
	if (value == 0)
	{
		if (errno == EINVAL || errno == ERANGE)
		{
			if (write_log)
			{
				snprintf(log_msg, LM_LEN,
					"\n! Error: failed to convert characters to value [ %s ]\n",
					strerror(errno));
				writelog(log_msg);
			}
			snprintf(LAST_ERROR, LE_LEN, "Error: failed to convert characters to value");
			errno = 0;
			return FALSE;
		}
	}
	return TRUE;
}

/*
  Add a PT message to the receive FIFO queue
*/
static int queue_msg(PASSTHRU_MSG *mBuf)
{
	fifo_msg_t *new_msg = (fifo_msg_t*)malloc(sizeof(fifo_msg_t));
	if (new_msg)
	{
		new_msg->pt_msg = mBuf;
		new_msg->next_in_q = NULL;
		if (fifo_head)
		{
			fifo_msg_t *temp = fifo_head;
			// find last in queue
			while (temp->next_in_q != NULL)
				temp = temp->next_in_q;
			// assign the new msg to last in queue
			temp->next_in_q = new_msg;
		}
		else
		{
			// new msg is now the first in the queue
			fifo_head = new_msg;
		}
		if (write_log)
			writelog("\tNew message queued\n");
		return TRUE;
	}
	return FALSE;
}

/*
  Read a PT message from the receive FIFO queue.
*/
static int read_queue_msg(PASSTHRU_MSG *mBuf)
{
	fifo_msg_t *temp = fifo_head;
	if (temp)
	{
		fifo_head = fifo_head->next_in_q;
		if (write_log)
			writelog("\tMessage dequeued\n");

		if (temp->pt_msg)
		{
			memcpy(mBuf, temp->pt_msg, sizeof(PASSTHRU_MSG));
			free(temp->pt_msg);
			free(temp);
			if (write_log)
				writelogpassthrumsg(mBuf);
			return TRUE;
		}
		else
			if (write_log)
				writelog("\tPT msg is NULL\n");
	}
	return FALSE;
}

/*
  Flush the receive FIFO queue.
*/
static void flush_queue()
{
	fifo_msg_t *current = NULL;
	while (fifo_head)
	{
		current = fifo_head;
		if (current->pt_msg)
			free(current->pt_msg);
		fifo_head = current->next_in_q;
		free(current);
	}
	if (write_log)
		writelog("\tReceive FIFO queue flushed\n");
}

/*
  Serach through data to find a pattern match.
  Return offset in data where first match is found or -1
  if not found.
*/
static int pattern_search(const uint8_t *data, const int data_len, const uint8_t *pattern)
{
	if (data_len < (int)strlen(pattern))
		return -1;

	int search_len = data_len - (int)strlen(pattern) + 1;
	int i = 0;
	for (; i < search_len; ++i)
	{
		if (data[i] != pattern[0])
			continue;

		// first byte matched, test for the rest of the pattern
		int j = (int)strlen(pattern) - 1;
		for (; j >= 1; j--)
		{
			if (data[i + j] != pattern[j])
				break;
			if (j == 1)
				return i;
		}
	}
	return -1;
}

/*
  Send data and expect to receive a reply, using specified timeout.
  If expect is NULL then command is acknowledged by aro response.
*/
static int usb_send_expect(uint8_t *data, const size_t len,
	const int capacity, const uint32_t timeout, const uint8_t *expect)
{
	int bytes_written = 0, r = LIBUSB_SUCCESS;

	// send data only if there is more than 0 bytes to send
	if (len > 0 && len <= (size_t)capacity)
	{
		r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_out,
			data, (int)len, &bytes_written, timeout);
		if (write_log)
		{
			writelog("\tUSB stream Sent:\n\t\t");
			if (bytes_written > 0)
				writelogmsg(data, 0, bytes_written);
			else
				writelog("bytes_written: 0, no USB stream Sent");
			writelog("\n");
		}
	}
	if (r != LIBUSB_SUCCESS)
	{
		if (write_log)
		{
			snprintf(log_msg, LM_LEN, "\tSend Error: %s\n", libusb_error_name(r));
			writelog(log_msg);
		}
		snprintf(LAST_ERROR, LE_LEN,
			"USB data transfer error sending %d bytes: %s", (int)len, libusb_error_name(r));
	}
	else
	{
		if (timeout > 0) // expect a reply otherwise return without reading
		{
			int need_aro = TRUE;
			int need_exp = FALSE;
			// If expect value is passed then the check for ARO is not required
			if (expect)
			{
				need_exp = TRUE;
				need_aro = FALSE;
			}

			int bytes_read = 0;
			int get_next = TRUE;
			while (get_next)
			{
				r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
					data, capacity, &bytes_read, timeout);
				if (bytes_read < capacity)
					data[bytes_read] = '\0';

				if (r != LIBUSB_SUCCESS)
				{
					if (write_log)
					{
						snprintf(log_msg, LM_LEN, "\tReceive Error: %s\n", libusb_error_name(r));
						writelog(log_msg);
					}
					snprintf(LAST_ERROR, LE_LEN, "USB data transfer error: %s", libusb_error_name(r));
					return r;
				}

				if (write_log)
				{
					writelog("\tUSB stream Rcvd:\n\t\t");
					if (bytes_read > 0)
						writelogmsg(data, 0, bytes_read);
					else
						writelog("bytes_read: 0, USB stream Rcvd");
					writelog("\n");
				}

				if (data[2] == 0x65)	// e
				{
					unsigned long errnum = strtoul(data + 4, NULL, 10);
					if (is_valid(errnum))
					{
						snprintf(LAST_ERROR, LE_LEN, "Error: J2534 device comms error: %lu", errnum);
						return errnum;
					}
				}

				if (need_aro)
				{
					int aro_pos = pattern_search(data, bytes_read, "aro\r\n");
					if (aro_pos >= 0)
					{
						need_aro = FALSE;
						get_next = FALSE;
						if (write_log)
							writelog("\t\tCommand acknowledged\n");
					}
				}

				if (need_exp)
				{
					int exp_pos = pattern_search(data, bytes_read, expect);
					if (exp_pos >= 0)
					{
						need_exp = FALSE;
						get_next = FALSE;
						if (write_log)
							writelog("\t\tAcknowledged by expect\n");
					}
				}
			}
		}
	}
	return r;
}

/*
  Establish a connection with a PassThru device.
 */
int32_t PassThruOpen(const void *pName, unsigned long *pDeviceID)
{
	if (pDeviceID == NULL)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error initializing J2534 library: pDeviceID must not be NULL");
		return J2534_ERR_NULL_PARAMETER;
	}

	const char *le = getenv("LOG_ENABLE");
	if (le)
	{
		if (le[0] == '0')
			write_log = FALSE;
		else
			logfile = fopen(le, "a");
		if (logfile)
			write_log = TRUE;
	}

	littleEndian = isLittleEndian();
	if (write_log)
	{
		writelog("Opening...\n\t|\n\tDevice Name: ");
		if (pName == NULL)
			writelog("NULL");
		else
			writelog((int8_t*)pName);
		writelog("\n");
	}

	con->ctx = NULL;	// use default context
	int r = libusb_init(&con->ctx);
	if (r != LIBUSB_SUCCESS)
	{
		// there was an error
		if (write_log)
		{
			snprintf(log_msg, LM_LEN, "\tInit Error: %s\n", libusb_error_name(r));
			writelog(log_msg);
		}
		snprintf(LAST_ERROR, LE_LEN, "Error initializing USB library: %s", libusb_error_name(r));
		libusb_exit(con->ctx);
		return error_map(r);
	}

	libusb_device **devs;
	ssize_t cnt = libusb_get_device_list(con->ctx, &devs);
	if (cnt < 0)
	{
		if (write_log)
			writelog("\tError getting device list\n");
		snprintf(LAST_ERROR, LE_LEN, "Error getting USB device list");
		libusb_exit(con->ctx);
		return J2534_ERR_DEVICE_NOT_CONNECTED;
	}

	r = open_dev_endpoints(devs, cnt, VENDOR_ID, PRODUCT_ID, endpoint);
	libusb_free_device_list(devs, 1);
	if (r != LIBUSB_SUCCESS)
	{
		if (write_log)
		{
			snprintf(log_msg, LM_LEN, "\tCannot find device, error: %s\n", libusb_error_name(r));
			writelog(log_msg);
		}
		snprintf(LAST_ERROR, LE_LEN, "Cannot find device: %s", libusb_error_name(r));
		libusb_exit(con->ctx);
		return J2534_ERR_DEVICE_NOT_CONNECTED;
	}

	if (con->dev_handle == NULL)
	{
		if (write_log)
			writelog("\tCannot open device handle\n");
		snprintf(LAST_ERROR, LE_LEN, "Cannot open device handle (disconnected?)");
		libusb_exit(con->ctx);
		return J2534_ERR_DEVICE_NOT_CONNECTED;
	}
	else
	{
		*pDeviceID = con->device_id;
		if (write_log)
		{
			snprintf(log_msg, LM_LEN, "\tDeviceID %lu opened\n", *pDeviceID);
			writelog(log_msg);
		}
	}

	//find out if kernel driver is attached
	if (libusb_kernel_driver_active(con->dev_handle, 0) == 1)
	{
		if (write_log)
			writelog("\tKernel Driver Active\n");
		if (libusb_detach_kernel_driver(con->dev_handle, 0) == 0) //detach it
			if (write_log)
				writelog("\tKernel Driver Detached\n");
	}

	//claim interface
	r = libusb_claim_interface(con->dev_handle, endpoint->intf_num);
	if (r != LIBUSB_SUCCESS)
	{
		if (write_log)
			writelog("\tCannot Claim Interface\n");
		snprintf(LAST_ERROR, LE_LEN, "Cannot claim interface from kernel driver");
		libusb_close(con->dev_handle);
		libusb_exit(con->ctx);
		return error_map(r);
	}
	if (write_log)
	{
		snprintf(log_msg, LM_LEN, "\tClaimed Interface %u\n", endpoint->intf_num);
		writelog(log_msg);
	}

	uint8_t data[MAX_LEN];
	// init device
	strcpy(data, "\r\n\r\nati\r\n");

	// expect ari with FW version
	r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, "ari ");
	if (r == LIBUSB_SUCCESS)
		memcpy(fw_version, data, 80);

	// open the device
	strcpy(data, "ata\r\n");
	r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, NULL);

	if (write_log && r == LIBUSB_SUCCESS)
		writelog("\tInit acknowledged\nInterface Opened\n");
	LAST_ERROR[0] = '\0';
	return J2534_NOERROR;
}

/*
  Terminate a connection with a Pass-Thru device.
 */
int32_t PassThruClose(const unsigned long DeviceID)
{
	if (write_log)
	{
		snprintf(log_msg, LM_LEN, "Closing...\n\t|\n\tDeviceID:  %lu\n", DeviceID);
		writelog(log_msg);
	}

	int r = J2534_NOERROR;
	if ((uint8_t)DeviceID != con->device_id)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Invalid DeviceID");
		r = J2534_ERR_INVALID_DEVICE_ID;
	}
	else
	{
		uint8_t data[MAX_LEN];
		strcpy(data, "atz\r\n");
		int r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, NULL);
		r = libusb_release_interface(con->dev_handle, endpoint->intf_num);
		libusb_close(con->dev_handle);
		libusb_exit(con->ctx);

		if (write_log)
		{
			writelog("Closed\n");
			fclose(logfile);
		}
	}
	return r;
}

/*
  Establish a connection using a protocol channel.
 */
int32_t PassThruConnect(const unsigned long DeviceID, const unsigned long protocolID,
	const unsigned long flags, const unsigned long baud, unsigned long *pChannelID)
{
	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"Connecting...\n\t|\n"
			"\tDeviceID:\t%lu\n"
			"\tprotocolID:\t%lu\n"
			"\tflags:\t\t%08lX\n"
			"\tbaud:\t\t%lu\n"
			, DeviceID, protocolID, flags, baud);
		writelog(log_msg);
	}

	if ((uint8_t)DeviceID != con->device_id)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Invalid DeviceID");
		return J2534_ERR_INVALID_DEVICE_ID;
	}

	switch ((int)protocolID) {
	case 3:
		con->channel = ISO9141;
		break;
	case 4:
		con->channel = ISO14230;
		break;
	case 5:
		con->channel = CAN;
		break;
	case 6:
		con->channel = ISO15765;
		break;
	default:
		return J2534_ERR_INVALID_PROTOCOL_ID;
	}

	int r = J2534_NOERROR;
	uint8_t data[MAX_LEN];
	snprintf(data, MAX_LEN, "ato%lu %lu %lu 0\r\n", protocolID, flags, baud);
	r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, NULL);
	*pChannelID = protocolID;
	con->protocol_id = protocolID;
	if (write_log && r == LIBUSB_SUCCESS)
		writelog("Connected\n");
	return error_map(r);
}

/*
  Terminate a connection with a protocol channel.
 */
int32_t PassThruDisconnect(const unsigned long ChannelID)
{
	if (write_log)
	{
		snprintf(log_msg, LM_LEN, "Disconnecting\n\t|\n\tChannelID: %lu\n", ChannelID);
		writelog(log_msg);
	}

	if (ChannelID != strtoul(&con->channel, NULL, 10))
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Invalid ChannelID");
		return J2534_ERR_INVALID_CHANNEL_ID;
	}

	// If any messages in the FIFO queue delete them
	flush_queue();

	uint8_t data[MAX_LEN];
	snprintf(data, MAX_LEN, "atc%lu\r\n", ChannelID);
	int r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, NULL);

	if (write_log && r == LIBUSB_SUCCESS)
		writelog("Disconnected\n");
	return error_map(r);
}

/*
  Read message(s) from a protocol channel.
 */
int32_t PassThruReadMsgs(const unsigned long ChannelID, PASSTHRU_MSG *pMsg,
	unsigned long *pNumMsgs, const unsigned long Timeout)
{
	if (pMsg == NULL)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: *pMsg must not be NULL");
		return J2534_ERR_NULL_PARAMETER;
	}

	if (ChannelID != strtoul(&con->channel, NULL, 10))
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Invalid ChannelID");
		return J2534_ERR_INVALID_CHANNEL_ID;
	}

	int8_t channel = con->channel;
	uint32_t timeout = Timeout;
	unsigned long msg_cnt = *pNumMsgs;	// number of msgs to read into pMsg array
	rx_buf_idx = 0;

	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"ReadMsgs\n\t|\n"
			"\tChannelID:\t%lu\n"
			"\tpNumMsgs:\t%lu\n"
			"\tTimeout:\t%u msec\n"
			"\trxBufIndex:\t%lu\n",
			ChannelID, msg_cnt, timeout, rx_buf_idx);
		writelog(log_msg);
	}

	*pNumMsgs = 0;
	PASSTHRU_MSG *msgBuf = pMsg;	// local copy for pointer arithmetic

	// Any messages in the FIFO queue to send?
	for (; msg_cnt; msg_cnt--)
	{
		if (read_queue_msg(msgBuf))
		{
			(*pNumMsgs)++;	// count the dequeued message
			msgBuf++;		// increment to next PT Msg
		}
		else
			break;
	}

	if (!msg_cnt)
	{
		if (write_log)
		{
			snprintf(log_msg, LM_LEN, "\tRX Buffers remaining:\t%lu\nEndReadMsg\n", msg_cnt);
			writelog(log_msg);
		}
		return J2534_NOERROR;
	}

	int bytes_read = 0, r = LIBUSB_SUCCESS;
	if (rx_buf_idx < msg_cnt)	// if pMsg array is not full, read from USB
	{
		uint8_t data[PM_DATA_LEN];
		int dontexit = TRUE;
		msgBuf->DataSize = 0;	// Initialize msg datasize

		while (dontexit)
		{
			//dontexit--;
			// Try to read USB
			r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
				data, PM_DATA_LEN, &bytes_read, timeout);

			if (r != LIBUSB_SUCCESS)
			{
				if (write_log)
				{
					snprintf(log_msg, LM_LEN, "\tRead Error: %s\n", libusb_error_name(r));
					writelog(log_msg);
				}
				snprintf(LAST_ERROR, LE_LEN, "USB data transfer error: %s", libusb_error_name(r));
				return error_map(r);
			}

			uint32_t pos = 5;	// index within "data" array to start a copy from
			uint32_t len = 3;	// index of the packet length for the current message in the "data" array
			int bytes_processed = 0;	// the number of bytes processed in the "data" array
			if (write_log)
			{
				snprintf(log_msg, LM_LEN,
					"\t\t*** USB READ: pos:%u, len:%u, bytes_processed:%d, bytes_read:%d, USB:%s\n\t\t",
					pos, len, bytes_processed, bytes_read, libusb_error_name(r));
				writelog(log_msg);
				writelogmsg(data, 0, bytes_read);
				writelog("\n");
			}

			if (bytes_read > 0 && bytes_read <= PM_DATA_LEN)
			{
				if (write_log)
				{
					snprintf(log_msg, LM_LEN,
						"\t\t=== %02X | %02X | %02X <> %02X | %02X ===\n",
						(uint8_t)data[0], (uint8_t)data[1],
						(uint8_t)data[2], (uint8_t)data[3],
						(uint8_t)data[4]);
					writelog(log_msg);
				}

				// Process an expected data packet
				while (bytes_processed < bytes_read
					&& data[bytes_processed + 0] == 0x61		// A
					&& data[bytes_processed + 1] == 0x72		// R
					&& (data[bytes_processed + 2] == channel
						|| data[bytes_processed + 2] == 0x6F))	// O
				{
					// If third byte equals the channel #, then this is Message data
					if (data[bytes_processed + 2] == channel)
					{
						uint8_t channel_id = data[bytes_processed + 2];
						uint8_t packet_type = data[bytes_processed + 4];
						int8_t *msg_type = "";
						unsigned long dataSize = 0;

						// Message Type check
						// TxDone Msg 0x10
						if (packet_type == TX_DONE)
						{
							msgBuf->Timestamp = parse_ts(data + pos);
							if (channel_id == ISO15765)	// CAN message
							{
								datacopy(msgBuf, data, 4, pos, (data[len] - 1));
								dataSize = msgBuf->DataSize + (data[len] - 5);
								msgBuf->DataSize = dataSize;
								msgBuf->ExtraDataIndex = 0;
								msgBuf->RxStatus = 8;	// TX Done
							}
							msgBuf->ProtocolID = con->protocol_id;
							msgBuf->TxFlags = 0;

							bytes_processed = bytes_processed + data[len] + 4;
							pos = bytes_processed + 5;
							len = bytes_processed + 3;
							if (write_log)
							{
								snprintf(log_msg, LM_LEN,
									"\t\t\t-- PROCESSED TX Done: pos:%u, len:%u, bytes_processed:%d, bytes_read:%d, ts:%08lX, msg_cnt:%lu\n",
									pos, len, bytes_processed, bytes_read, msgBuf->Timestamp, rx_buf_idx + 1);
								writelog(log_msg);
							}
							rx_buf_idx++;
							if (rx_buf_idx < msg_cnt)
							{
								msgBuf++;
								msgBuf->DataSize = 0;	// Initialize new msg datasize
							}
							dontexit = FALSE;
						}

						// Start of a TX LB Msg 0xA0 or Normal Msg 0x80 Indication
						else if (packet_type == TX_LB_START_IND || packet_type == NORM_MSG_START_IND)
						{
							msgBuf->Timestamp = parse_ts(data + pos);
							if (channel_id == CAN || channel_id == ISO15765)	// CAN message
							{
								datacopy(msgBuf, data, 4, pos, (data[len] - 1));
								dataSize = msgBuf->DataSize + (data[len] - 5);
								msgBuf->DataSize = dataSize;
								msgBuf->ExtraDataIndex = 0;
								msgBuf->RxStatus = 9;	// TX Done Loopback
							}
							if (channel_id == ISO9141 || channel_id == ISO14230)	// K-line message
							{
								msgBuf->DataSize = 0;
								msgBuf->ExtraDataIndex = 0;
								msgBuf->RxStatus = 2;	// Msg start indication
							}
							msgBuf->ProtocolID = con->protocol_id;
							msgBuf->TxFlags = 0;

							bytes_processed = bytes_processed + data[len] + 4;
							pos = bytes_processed + 5;
							len = bytes_processed + 3;
							if (write_log)
							{
								if (packet_type == TX_LB_START_IND)
									msg_type = "TX LB";
								if (packet_type == NORM_MSG_START_IND)
									msg_type = "RX";

								snprintf(log_msg, LM_LEN,
									"\t\t\t-- PROCESSED %s Msg INDICATION: pos:%u, len:%u, bytes_processed:%d, bytes_read:%d, ts:%08lX, msg_cnt:%lu\n",
									msg_type, pos, len, bytes_processed, bytes_read, msgBuf->Timestamp, rx_buf_idx+1);
								writelog(log_msg);
							}
							rx_buf_idx++;
							if (rx_buf_idx < msg_cnt)
							{
								msgBuf++;
								msgBuf->DataSize = 0;	// Initialize new msg datasize
							}
							dontexit = FALSE;
						}

						// TX LB 0x20 or Normal 0x00 Message
						else if (packet_type == TX_LB_MSG || packet_type == NORM_MSG)
						{
							msgBuf->RxStatus = 0;		// normal msg status
							if (packet_type == TX_LB_MSG)
								msgBuf->RxStatus = 1;	// TX Loopback msg status

							if (channel_id == CAN || channel_id == ISO15765)	// CAN message
							{
								msgBuf->Timestamp = parse_ts(data + pos);
								datacopy(msgBuf, data, 4, pos, (data[len] - 1));
								dataSize = msgBuf->DataSize + (data[len] - 5);
								msgBuf->DataSize = dataSize;
								msgBuf->ExtraDataIndex = dataSize;
							}
							if (channel_id == ISO9141 || channel_id == ISO14230)	// K-line message
							{
								datacopy(msgBuf, data, 0, pos, (data[len] - 1));
								dataSize = msgBuf->DataSize + (data[len] - 1);
								msgBuf->DataSize = dataSize;
								msgBuf->ExtraDataIndex = dataSize;
							}
							msgBuf->ProtocolID = con->protocol_id;
							msgBuf->TxFlags = 0;
							bytes_processed = bytes_processed + data[len] + 4;
							pos = bytes_processed + 5;
							len = bytes_processed + 3;
							if (write_log)
							{
								if (packet_type == TX_LB_MSG)
									msg_type = "LB";
								if (packet_type == NORM_MSG)
									msg_type = "RX";

								snprintf(log_msg, LM_LEN,
									"\t\t\t-- READ %s Msg: pos:%u, len:%u, bytes_processed:%d, bytes_read:%d, DataSize:%lu, msg_cnt:%lu\n",
									msg_type, pos, len, bytes_processed, bytes_read, dataSize, rx_buf_idx+1);
								writelog(log_msg);
							}
							if (channel_id == CAN)	// CAN message
							{
								rx_buf_idx++;
								if (rx_buf_idx < msg_cnt)
								{
									msgBuf++;
									msgBuf->DataSize = 0;	// Initialize new msg datasize
								}
								dontexit = FALSE;
							}
							else
								dontexit = TRUE;	// Read next message to get End indication and timestamp
						}

						// End of RX 0x40, ExtAddr RX 0x44 or LB 0x60 Msg End Indication
						else if (packet_type == RX_MSG_END_IND || packet_type == EXT_ADDR_MSG_END_IND || packet_type == LB_MSG_END_IND)
						{
							if (write_log)
							{
								if (packet_type == RX_MSG_END_IND)
									msg_type = "RX";
								if (packet_type == EXT_ADDR_MSG_END_IND)
									msg_type = "Ext Addr RX";
								if (packet_type == LB_MSG_END_IND)
									msg_type = "LB";

								snprintf(log_msg, LM_LEN,
									"\t\t\t-- %s Msg END INDICATION: pos:%u, len:%u, bytes_processed:%d, bytes_read:%d\n",
									msg_type, pos, len, bytes_processed, bytes_read);
								writelog(log_msg);
							}
							msgBuf->Timestamp = parse_ts(data + pos);
							if (channel_id == CAN || channel_id == ISO15765)	// CAN message
							{
								datacopy(msgBuf, data, 4, pos, (data[len] - 1));
								dataSize = msgBuf->DataSize + (data[len] - 5);
								msgBuf->DataSize = dataSize;
								msgBuf->ExtraDataIndex = dataSize;
								msgBuf->RxStatus = 0;	// RX Indication
							}
							bytes_processed = bytes_processed + data[len] + 4;
							pos = bytes_processed + 5;
							len = bytes_processed + 3;
							if (write_log)
							{
								snprintf(log_msg, LM_LEN,
									"\t\t\t-- PROCESSED %s END INDICATION: pos:%u, len:%u, bytes_processed:%d, bytes_read:%d, ts:%08lX, msg_cnt:%lu\n",
									msg_type, pos, len, bytes_processed, bytes_read, msgBuf->Timestamp, rx_buf_idx+1);
								writelog(log_msg);
							}
							rx_buf_idx++;
							if (rx_buf_idx < msg_cnt)
							{
								msgBuf++;
								msgBuf->DataSize = 0;	// Initialize new msg datasize
							}
							dontexit = FALSE;
						}
						else
						{
							if (write_log)
							{
								snprintf(log_msg, LM_LEN,
									"\t\t\t-- Unprocessed data length (data[len] = %02X)\n\t\t\t  ",
									data[len]);
								writelog(log_msg);
								writelogmsg(data, bytes_processed + 5, bytes_read - bytes_processed);
								writelog("\n");
							}
							bytes_processed = bytes_processed + data[len] + 4;
							pos = bytes_processed + 5;
							len = bytes_processed + 3;
							if (write_log)
							{
								snprintf(log_msg, LM_LEN,
									"\t\t\t-- DEFAULT: pos:%u, len:%u, bytes_processed:%d, bytes_read:%d, msg_cnt:%lu\n",
									pos, len, bytes_processed, bytes_read, rx_buf_idx);
								writelog(log_msg);
							}
						} // End of message type check
					}	// End of the AR channel# packet

					// If third byte equals 'O', then this is Acknowledgement data
					if (data[bytes_processed + 2] == 0x6F)	// O
					{
						bytes_processed = bytes_processed + 5;
						pos = bytes_processed + 5;
						len = bytes_processed + 3;
						if (write_log)
						{
							snprintf(log_msg, LM_LEN,
								"\t\t\t-- ARO Msg: pos:%u, len:%u, bytes_processed:%d, bytes_read:%d, msg_cnt:%lu\n",
								pos, len, bytes_processed, bytes_read, rx_buf_idx);
							writelog(log_msg);
						}
					}

					// Check if we have read msg_cnt messages,
					// if so, pMsg array is full, try to queue the message
					if (bytes_processed < bytes_read && rx_buf_idx >= msg_cnt)
					{
						if (write_log)
						{
							snprintf(log_msg, LM_LEN, "\tRead message array full, %d bytes remaining\n",
								bytes_read - bytes_processed);
							writelog(log_msg);
						}

						// Try to queue the remaining bytes into new PT messages
						msgBuf = (PASSTHRU_MSG*)malloc(sizeof(PASSTHRU_MSG));
						if (msgBuf)
						{
							msgBuf->DataSize = 0;	// Initialize new msg datasize
							if (write_log)
								writelog("\tMore data, let's queue it\n");
							if (queue_msg(msgBuf))
								rx_buf_idx--;	// message queued so don't count it
							else
							{
								// couldn't allocate memory in queue, skip remaining bytes and free msgBuf
								bytes_processed = bytes_read;
								dontexit = FALSE;
								free(msgBuf);
							}
						}
						else
						{
							// couldn't allocate memory, skip remaining bytes
							bytes_processed = bytes_read;
							dontexit = FALSE;
						}
					}	// End of data packet type processing
				}	// End of bytes to process
			}	// End of bytes read
		}	// End of dontexit
	}	// End of read msg_cnt messages

	// add dequeued msg count to USB read message count
	*pNumMsgs += rx_buf_idx;
	if (write_log)
	{
		uint32_t i = 0;
		for (; i < *pNumMsgs; i++)
		{
			writelogpassthrumsg(pMsg + i);
		}
		snprintf(log_msg, LM_LEN, "\tRX Buffers remaining:\t%lu\nEndReadMsg\n",
			msg_cnt - rx_buf_idx);
		writelog(log_msg);
	}
	if (rx_buf_idx > msg_cnt)
	{
		return J2534_ERR_BUFFER_OVERFLOW;
	}
	return J2534_NOERROR;
}

/*
  Write message(s) to a protocol channel.
 */
int32_t PassThruWriteMsgs(const unsigned long ChannelID, const PASSTHRU_MSG *pMsg,
	unsigned long *pNumMsgs, const unsigned long timeInterval)
{
	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"WriteMsgs\n\t|\n"
			"\tChannelID:\t%lu\n"
			"\tpNumMsgs:\t%lu\n"
			"\tTimeout:\t%lu msec\n",
			ChannelID, *pNumMsgs, timeInterval);
		writelog(log_msg);
		writelogpassthrumsg(pMsg);
	}

	unsigned long msg_cnt = *pNumMsgs, i = 0, msg_data_size = 0;
	int r = LIBUSB_SUCCESS;
	uint8_t data[PM_DATA_LEN];
	size_t strln = 0;
	*pNumMsgs = 0;

	while (i < msg_cnt)
	{
		msg_data_size = pMsg[i].DataSize;

		if (msg_data_size > 0 && msg_data_size <= PM_DATA_LEN)
		{
			snprintf(data, PM_DATA_LEN, "att%lu %lu %lu\r\n", ChannelID, pMsg[i].DataSize, pMsg[i].TxFlags);

			strln = strlen(data);
			uint32_t j = 0;
			while (j < msg_data_size)
				data[strln++] = pMsg[i].Data[j++];

			r = usb_send_expect(data, strln, PM_DATA_LEN, 0, NULL);
		}
		else
		{
			if (write_log)
			{
				snprintf(log_msg, LM_LEN, "\tInvalid message size: %lu\n", msg_data_size);
				writelog(log_msg);
			}
			snprintf(LAST_ERROR, LE_LEN, "Invalid message size: %lu", msg_data_size);
			return J2534_ERR_INVALID_MSG;
		}
		*pNumMsgs = ++i;
	}
	if (write_log)
		writelog("EndWriteMsgs\n");
	return error_map(r);
}

/*
  Start sending a message at a specified time interval on a protocol channel.
 */
int32_t PassThruStartPeriodicMsg(const unsigned long ChannelID, const PASSTHRU_MSG *pMsg,
	const unsigned long *pMsgID, const unsigned long timeInterval)
{
	if (write_log)
		writelog("StartPeriodic, not supported\n");
	return J2534_ERR_NOT_SUPPORTED;
}

/*
  Stop a periodic message.
 */
int32_t PassThruStopPeriodicMsg(const unsigned long ChannelID, const unsigned long msgID)
{
	if (write_log)
		writelog("StopPeriodic, not supported\n");
	return J2534_ERR_NOT_SUPPORTED;
}

/*
  Start filtering incoming messages on a protocol channel.
 */
int32_t PassThruStartMsgFilter(const unsigned long ChannelID, const unsigned long FilterType,
	const PASSTHRU_MSG *pMaskMsg, const PASSTHRU_MSG *pPatternMsg,
	const PASSTHRU_MSG *pFlowControlMsg, unsigned long *pMsgID)
{
	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"StartMsgFilter\n\t|\n"
			"\tChannelID:\t%lu\n"
			"\tFilterType:\t%lu\n"
			"\tpMaskMsg:\n",
			ChannelID, FilterType);
		writelog(log_msg);
		writelogpassthrumsg(pMaskMsg);
		writelog("\n\tpPatternMsg:\n");
		writelogpassthrumsg(pPatternMsg);
		writelog("\n\tpFlowControlMsg:\n");
		if (pFlowControlMsg == NULL)
			writelog("\tNULL");
		else
			writelogpassthrumsg(pFlowControlMsg);
		writelog("\n");
	}

	if (pMaskMsg == NULL || pPatternMsg == NULL)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: PASSTHRU_MSG* must not be NULL");
		return J2534_ERR_NULL_PARAMETER;
	}
	if (pMaskMsg->DataSize > 12 || pPatternMsg->DataSize > 12)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: PASSTHRU_MSG invalid data length");
		return J2534_ERR_INVALID_MSG;
	}
	if (pMaskMsg->DataSize != pPatternMsg->DataSize)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Mask and Pattern have differnt data lenghts");
		return J2534_ERR_INVALID_MSG;
	}
	if (pMaskMsg->TxFlags != pPatternMsg->TxFlags)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Mask and Pattern have differnt TX flags");
		return J2534_ERR_INVALID_MSG;
	}
	if ((FilterType == J2534_PASS_FILTER || FilterType == J2534_BLOCK_FILTER)
		&& pFlowControlMsg)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: FilterType, FlowControlMsg mismatch");
		return J2534_ERR_INVALID_MSG;
	}

	uint8_t data[MAX_LEN];
	snprintf(data, MAX_LEN, "atf%lu %lu %lu %lu\r\n", ChannelID,
		FilterType, pMaskMsg->TxFlags, pMaskMsg->DataSize);

	// append the mask, pattern and flow control bytes keeping track of the final byte count
	size_t j = 0;
	size_t datalen = strlen(data);
	size_t i = datalen;
	datalen = datalen + pMaskMsg->DataSize;
	while (i < datalen)
		data[i++] = pMaskMsg->Data[j++];

	datalen = i + pPatternMsg->DataSize;
	j = 0;
	while (i < datalen)
		data[i++] = pPatternMsg->Data[j++];

	if (pFlowControlMsg)
	{
		datalen = i + pFlowControlMsg->DataSize;
		j = 0;
		while (i < datalen)
			data[i++] = pFlowControlMsg->Data[j++];
	}

	int r = usb_send_expect(data, i, MAX_LEN, 2000, "arf");

	int failed = FALSE;
	int8_t *word = strtok(data, DELIMITERS);
	if (word)
	{
		word = strtok(NULL, DELIMITERS);
		if (word)
		{
			unsigned long lval = strtoul(word, NULL, 10);
			if (is_valid(lval))
				*pMsgID = lval;
			else
				failed = TRUE;
		}
		else
			failed = TRUE;
	}
	else
		failed = TRUE;

	if (failed)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: failed to parse reply");
		r = J2534_ERR_FAILED;
	}

	if (write_log)
		writelog("EndStartMsgFilter\n");
	return error_map(r);
}

/*
  Stops filtering incoming messages on a protocol channel.
 */
int32_t PassThruStopMsgFilter(const unsigned long ChannelID, const unsigned long msgID)
{
	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"StopMsgFilter\n\t|\n"
			"\tChannelID:\t%lu\n"
			"\tmsgID:\t\t%lu\n",
			ChannelID, msgID);
		writelog(log_msg);
	}

	int r = J2534_NOERROR;
	if (ChannelID != strtoul(&con->channel, NULL, 10))
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Invalid ChannelID");
		r = J2534_ERR_INVALID_CHANNEL_ID;
	}
	else
	{
		uint8_t data[MAX_LEN];
		snprintf(data, MAX_LEN, "atk%lu %lu\r\n", ChannelID, msgID);
		r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, NULL);
	}
	if (write_log)
		writelog("EndStopMsgFilter\n");
	return error_map(r);
}

/*
  Set a programming voltage on a specific pin.
 */
int32_t PassThruSetProgrammingVoltage(const unsigned long DeviceID,
	const unsigned long pinNumber, const unsigned long voltage)
{
	if (write_log)
		writelog("SetProgrammingVoltage, not support\n");
	return J2534_ERR_NOT_SUPPORTED;
}

/*
  Reads the version information of the firmware, this DLL and API implmentation.
 */
int32_t PassThruReadVersion(const unsigned long DeviceID, char *pFirmwareVersion,
	char *pDllVersion, char *pApiVersion)
{
	if (pFirmwareVersion == NULL || pDllVersion == NULL
		|| pApiVersion == NULL)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Version* must not be NULL");
		return J2534_ERR_NULL_PARAMETER;
	}

	char dll_ver[MAX_LEN];

#ifdef GET_LIBUSB_VERSION
	// version info only available for libusb 1.0.10 and later
	const struct libusb_version *ver = libusb_get_version();
	snprintf(dll_ver, MAX_LEN, "%s (libusb-%u.%u.%u.%u%s)", DLL_VERSION,
		ver->major, ver->minor, ver->micro, ver->nano, ver->rc);
#else
	snprintf(dll_ver, MAX_LEN, "%s", DLL_VERSION);
#endif
	int failed = FALSE;
	if (fw_version[0] != 0)
	{
		char *pos = strrchr(fw_version, ':');
		if (pos)
		{
			char *word = strtok(pos + 1, DELIMITERS);
			if (word)
				strcpy(pFirmwareVersion, word);
			else
				failed = TRUE;
		}
		else
			failed = TRUE;
	}
	if (failed)
		strcpy(pFirmwareVersion, "unavailable");

	strcpy(pDllVersion, dll_ver);
	strcpy(pApiVersion, API_VERSION);

	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"ReadVersion\n\t|\n"
			"\tfwVer : %s\n"
			"\tlibVer: %s\n"
			"\tapiVer: %s\n"
			"EndReadVersion\n",
			pFirmwareVersion, pDllVersion, pApiVersion);
		writelog(log_msg);
	}
	return J2534_NOERROR;
}

/*
  Gets the text description of the last error.
 */
int32_t PassThruGetLastError(char *pErrorDescription)
{
	if (write_log)
		writelog("GetLastError\n\t|\n\tErrorDescription:\t");
	if (pErrorDescription == NULL)
	{
		if (write_log)
			writelog("NULL");
		return J2534_ERR_NULL_PARAMETER;
	}
	pErrorDescription = LAST_ERROR;
	if (write_log)
	{
		writelog(pErrorDescription);
		writelog("\nEndGetLastError\n");
	}
	return J2534_NOERROR;
}

/*
  General I/O control functions for reading and writing
  protocol configuration parameters (e.g. initialization,
  baud rates, programming voltages, etc.).
 */
int32_t PassThruIoctl(const unsigned long ChannelID, const unsigned long ioctlID,
	const void *pInput, void *pOutput)
{
	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"Ioctl\n\t|\n"
			"\tChannelID:\t%lu\n"
			"\tioctlID:\t%lu ",
			ChannelID, ioctlID);
		writelog(log_msg);
	}
	uint8_t data[MAX_LEN];
	ssize_t bytes_written = 0;
	size_t strln = 0;
	uint32_t i = 0, par_cnt = 0, bytes_read = 0;
	int r = LIBUSB_ERROR_NOT_SUPPORTED;
	if (ioctlID == J2534_GET_CONFIG)
	{
		const SCONFIG_LIST *inputlist = pInput;
		if (write_log)
		{
			snprintf(log_msg, LM_LEN,
				"[Config GET]\n\tNumOfParams: %lu\n",
				inputlist->NumOfParams);
			writelog(log_msg);
		}
		SCONFIG *cfgitem;
		par_cnt = inputlist->NumOfParams;
		for (i = 0; i < par_cnt; ++i)
		{
			cfgitem = &inputlist->ConfigPtr[i];
			snprintf(data, MAX_LEN, "atg%lu %lu\r\n", ChannelID, cfgitem->Parameter);
			r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, "arg");

			if (data[0] == 0x61		// a
				&& data[1] == 0x72	// r
				&& data[2] == 0x67)	// g
			{
				int8_t *word = strtok(data, DELIMITERS);
				if (word == NULL)
				{
					snprintf(LAST_ERROR, LE_LEN, "Error: failed to parse reply");
					r = J2534_ERR_FAILED;
					goto EXIT_IOCTL;
				}

				word = strtok(NULL, DELIMITERS);
				if (word == NULL)
				{
					snprintf(LAST_ERROR, LE_LEN, "Error: failed to parse reply");
					r = J2534_ERR_FAILED;
					goto EXIT_IOCTL;
				}

				unsigned long lval = strtoul(word, NULL, 10);
				if (is_valid(lval))
					cfgitem->Parameter = lval;

				word = strtok(NULL, DELIMITERS);
				if (word == NULL)
				{
					snprintf(LAST_ERROR, LE_LEN, "Error: failed to parse reply");
					r = J2534_ERR_FAILED;
					goto EXIT_IOCTL;
				}

				lval = strtoul(word, NULL, 10);
				if (is_valid(lval))
					cfgitem->Value = lval;

				if (write_log)
				{
					snprintf(log_msg, LM_LEN,
						"\t\tConfigItem(p,v): %02lX, %02lX\n",
						cfgitem->Parameter, cfgitem->Value);
					writelog(log_msg);
				}
			}
			else
			{
				snprintf(LAST_ERROR, LE_LEN, "Invalid parameter response");
				return J2534_ERR_INVALID_MSG;
			}
		}
	}
	if (ioctlID == J2534_SET_CONFIG)
	{
		const SCONFIG_LIST *inputlist = pInput;
		if (write_log)
		{
			snprintf(log_msg, LM_LEN,
				"[Config SET]\n\tNumOfParams: %lu\n",
				inputlist->NumOfParams);
			writelog(log_msg);
		}
		SCONFIG *cfgitem;
		par_cnt = inputlist->NumOfParams;
		for (i = 0; i < par_cnt; ++i)
		{
			cfgitem = &inputlist->ConfigPtr[i];
			snprintf(data, MAX_LEN, "ats%lu %lu %lu\r\n", ChannelID, cfgitem->Parameter, cfgitem->Value);
			if (write_log)
			{
				snprintf(log_msg, LM_LEN,
					"\t\tConfigItem(p,v): %02lX, %02lX\n",
					cfgitem->Parameter, cfgitem->Value);
				writelog(log_msg);
			}
			r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, NULL);
		}
	}
	if (ioctlID == J2534_READ_VBATT)
	{
		if (write_log)
			writelog("[READ_VBATT]\n");
		uint32_t *vBatt = pOutput;
		uint32_t pin = 16;
		snprintf(data, MAX_LEN, "atr %u\r\n", pin);
		r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, "arr ");

		int8_t *word = strtok(data, DELIMITERS);
		if (word == NULL)
		{
			snprintf(LAST_ERROR, LE_LEN, "Error: failed to parse reply");
			r = J2534_ERR_FAILED;
			goto EXIT_IOCTL;
		}

		word = strtok(NULL, DELIMITERS);
		if (word == NULL)
		{
			snprintf(LAST_ERROR, LE_LEN, "Error: failed to parse reply");
			r = J2534_ERR_FAILED;
			goto EXIT_IOCTL;
		}

		unsigned long lval = strtoul(word, NULL, 10);
		if (is_valid(lval))
		{
			if (pin == lval)
			{
				word = strtok(NULL, DELIMITERS);
				if (word == NULL)
				{
					snprintf(LAST_ERROR, LE_LEN, "Error: failed to parse reply");
					r = J2534_ERR_FAILED;
					goto EXIT_IOCTL;
				}

				lval = strtoul(word, NULL, 10);
				if (is_valid(lval))
					*vBatt = lval;
				else
				{
					r = J2534_ERR_FAILED;
					goto EXIT_IOCTL;
				}
			}
		}
		else
		{
			r = J2534_ERR_FAILED;
			goto EXIT_IOCTL;
		}

		if (write_log)
		{
			snprintf(log_msg, LM_LEN,
				"\t\tPin 16 Voltage:\t%umV\n", *vBatt);
			writelog(log_msg);
		}
	}
	if (ioctlID == J2534_FAST_INIT)
	{
		if (pInput == NULL || pOutput == NULL)
		{
			snprintf(LAST_ERROR, LE_LEN, "Error: pInput/Output must not be NULL");
			r = J2534_ERR_NULL_PARAMETER;
			goto EXIT_IOCTL;
		}

		const PASSTHRU_MSG *pMsg = pInput;
		unsigned long len = pMsg->DataSize;
		if (len > 0 && len <= MAX_LEN)
		{
			if (write_log)
			{
				writelog("[FAST INIT]\n");
				writelogpassthrumsg(pMsg);
			}
			snprintf(data, MAX_LEN, "aty%lu %lu 0\r\n", ChannelID, pMsg->DataSize);
			strln = strlen(data);
			for (i = 0; i < len; ++i)
				data[strln++] = pMsg->Data[i];

			r = usb_send_expect(data, strln, MAX_LEN, 2000, "ary");
			if (r != LIBUSB_SUCCESS)
				goto EXIT_IOCTL;

			len = strtoul(data + 5, NULL, 10);
			if (len == 0)
			{
				snprintf(LAST_ERROR, LE_LEN, "Error: failed to convert to long");
				r = J2534_ERR_FAILED;
				goto EXIT_IOCTL;
			}

			r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
				data, MAX_LEN, &bytes_read, 500);
			if (r != LIBUSB_SUCCESS)
			{
				snprintf(LAST_ERROR, LE_LEN, "Error: failed to read timing: %s",
					libusb_error_name(r));
				goto EXIT_IOCTL;
			}

			PASSTHRU_MSG *pOutMsg = pOutput;
			pOutMsg->DataSize = 0;
			datacopy(pOutMsg, data, 0, 0, len);
			pOutMsg->DataSize = len;
			pOutMsg->ExtraDataIndex = len;
			pOutMsg->RxStatus = 0;
			pOutMsg->ProtocolID = con->protocol_id;
			if (write_log)
				writelogpassthrumsg(pOutMsg);
		}
		else
		{
			if (write_log)
			{
				snprintf(log_msg, LM_LEN,
					"\tInvalid message size: %lu\n", len);
				writelog(log_msg);
			}
			snprintf(LAST_ERROR, LE_LEN, "Invalid message size: %lu", len);
			return J2534_ERR_INVALID_MSG;
		}
	}
	if (ioctlID == J2534_CLEAR_TX_BUFFER)
	{
		if (write_log)
			writelog("[CLEAR_TX_BUFFER]\n");
		r = LIBUSB_SUCCESS;
	}
	if (ioctlID == J2534_CLEAR_RX_BUFFER)
	{
		if (write_log)
			writelog("[CLEAR_RX_BUFFER]\n");

		// If any messages in the FIFO queue delete them
		flush_queue();

		r = LIBUSB_SUCCESS;
	}

	EXIT_IOCTL:
	if (write_log)
		writelog("EndIoctl\n");
	return error_map(r);
}
