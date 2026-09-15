/*! ----------------------------------------------------------------------------
 *  @file    main.c
 *  @brief   main loop for the DecaRanging application
 *
 * @attention
 *
 * Copyright 2015 (c) DecaWave Ltd, Dublin, Ireland.
 *
 * All rights reserved.
 *
 * @author DecaWave
 */

#include <Arduino.h>
#include "dw3000.h"
#include "dw3000_regs.h"
#include "dw3000_shared_defines.h"

#include "bphero_uwb.h"
#include "SPI.h"

//extern void usb_run(void);
//extern int usb_init(void);




/**
**===========================================================================
**
**  Abstract: main program
**
**===========================================================================
*/

static int ret;

static uint32_t status_reg = 0;

static uint8_t distance_seqnum;
static srd_msg_dsss *msg_f_recv;

// static uint16_t RX_ANT_DLY;
// #static uint16_t TX_ANT_DLY;

#define RX_ANT_DLY 0
#define TX_ANT_DLY 32880

#define CIR_LENGTH 3
#define COMPLEX_BYTE_LEN 6
#define LCD_BUFF_LEN (500)

static dwt_rxdiag_t rx_diag1;
static uint8_t usbVCOMout[LCD_BUFF_LEN * 8];

static uint8_t cir_buffer1[4 * CIR_LENGTH + 1];
static uint8_t cir_buffer2[4 * CIR_LENGTH + 1];
static uint8_t cir_buffer3[4 * CIR_LENGTH + 1];
static uint8_t cir_buffer4[4 * CIR_LENGTH + 1];


//extern dwt_config_t config2;
//extern dwt_txconfig_t txconfig2;
//extern uint16_t rfDelaysTREK[2];

struct cir_tap_struct
{
	uint16_t real;
	uint16_t imag;
};

// int App_Module_Uart_USB_Send(uint8_t *buf, uint16_t len)
// {
// 		int ret = 0;
// 		char send_buf[2000];
// 		memset(send_buf,0, sizeof(send_buf));
// 		memcpy(send_buf, buf, len);
// 		USART1_SendBuffer(send_buf, len, true);
// 		//HalUsbWrite(send_buf, len);
// 		return ret;
// }


DummyStream* dummy = nullptr;
DWUart* uart = nullptr;
DW3000Port* port = nullptr;
DW3000* radio = nullptr;


int psduLength = 0;
srd_msg_dsss msg_f_send;
//srd_msg_dsss msg_f_send2;

uint8_t rx_buffer[FRAME_LEN_MAX];
//uint32_t status_reg = 0;
static uint32_t frame_len = 0;

//I can only guess what this method does
void BPhero_UWB_Message_Init() {
    memset(&msg_f_send, 0, sizeof(msg_f_send));
}

//have to guess how these work
//final_msg_set_ts(&msg_f_send.messageData[FINAL_MSG_POLL_TX_TS_IDX], poll_tx_ts);
void final_msg_set_ts(uint8_t* ptr, uint64_t timestamp) {
	PacketHelpers::num_to_byte_array(timestamp, ptr, 5);
}
uint64_t final_msg_get_ts(uint8_t* ptr) {
	PacketHelpers::byte_array_to_num(ptr, 5);
}

//everything important is inside the loop section
void setup() {}


// #pragma GCC optimize ("O3")
#ifdef TX_NODE

static uint64_t poll_tx_ts;
static uint64_t cur_ts;
static uint64_t resp_rx_ts;
static uint64_t final_tx_ts;

static uint32_t final_tx_time;
static uint32_t final2_tx_time;

static int Final_Distance = 0;

static uint8_t board_num = 3;



//radio configuration structs, we need both channel 5 and 9 to do frequency hopping
static const dwt_config_t config_ch5 = {
    5,                		/* Channel number. */
    DWT_PLEN_64,     		/* Preamble length. Used in TX only. */
    DWT_PAC8,         		/* Preamble acquisition chunk size. Used in RX only. */
    9,                		/* TX preamble code. Used in TX only. */
    9,                		/* RX preamble code. Used in RX only. */
    1,                		/* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       		/* Data rate. */
    DWT_PHRMODE_STD,  		/* PHY header mode. */
    DWT_PHRRATE_STD,  		/* PHY header rate. */
    (64 + 1 + 8 - 8),    	/* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, 		/* STS disabled */
    DWT_STS_LEN_64,   		/* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       		/* PDOA mode off */
};

static const dwt_config_t config_ch9 = {
    9,                		/* Channel number. */
    DWT_PLEN_64,     		/* Preamble length. Used in TX only. */
    DWT_PAC8,         		/* Preamble acquisition chunk size. Used in RX only. */
    9,                		/* TX preamble code. Used in TX only. */
    9,                		/* RX preamble code. Used in RX only. */
    1,                		/* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       		/* Data rate. */
    DWT_PHRMODE_STD,  		/* PHY header mode. */
    DWT_PHRRATE_STD,  		/* PHY header rate. */
    (64 + 1 + 8 - 8),    	/* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, 		/* STS disabled */
    DWT_STS_LEN_64,   		/* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       		/* PDOA mode off */
};

dwt_txconfig_t txconfig_ch5 = {
    0x34,           /* PG delay. */
    0xfdfdfdfd,      /* TX power. */
    0x0             /*PG count*/
};

dwt_txconfig_t txconfig_ch9 = {
    0x34,           /* PG delay. */
    0xfefefefe,     /* TX power. */
    0x0             /*PG count*/
};







void loop(void)
{
	// sets up the device to use the pins on the bottom left of the rPi header for serial communication.
	Serial = Uart(NRF_UART0, UARTE0_UART0_IRQn, 31, 7);
	Serial.begin(BAUD_RATE);
	Serial.println("Begin");


	// sets up the SPI connection to the DW3000 radio
	SPI = SPIClass(NRF_SPI2, SPI_MISO, SPI_CLK, SPI_MOSI);
	SPI.begin();



    //hard reset
	port->reset();

	radio->dwt_softreset();

	while (!radio->dwt_checkidlerc()) // Need to make sure DW IC is in IDLE_RC before proceeding
	{
		Serial.println("Idle failed");
		delay(1000);
	}


	//SPI_ConfigFastRate(SPI_BaudRatePrescaler_32);

	// Initialize the DW1000
	// uses DWT_LOADUCODE, which we don't have documentation for
	if (radio->dwt_initialise(0) == DWT_ERROR)
	{
		while (1)
		{
			Serial.println("Init failed");
			delay(1000);
		};
	}


	//unsure if we need this for the DW3000.
	radio->dwt_setxtaltrim(16);


	//enable CIR collection
	radio->dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);

	// Adjust the SPI to 18 MHz.
	//SPI_ConfigFastRate(SPI_BaudRatePrescaler_4);
	// Configure the operating frequency

	//		config.txCode = 10;
	//		config.rxCode = 10;
	radio->dwt_configure(&config_ch5);
	// Activate the DW1000 status indicator.
	radio->dwt_setleds(1);
	// Configure transmit power
	radio->dwt_configuretxrf(&txconfig_ch5);
	// Set Work Network ID
	radio->dwt_setpanid(NET_PANID);
	// Set own short address
	radio->dwt_setaddress16(1);
	// Configure antenna delay
	radio->dwt_setrxaftertxdelay(RX_ANT_DLY);
	radio->dwt_settxantennadelay(TX_ANT_DLY);

	// Configuring the DW1000 interrupt, although it is not actually used.
	//radio->dwt_setinterrupt(DWT_INT_RFCG | (DWT_INT_ARFE | DWT_INT_RFSL | DWT_INT_SFDT | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFTO /*| DWT_INT_RXPTO*/), 1);

	//wipes buffer bits. Not sure if it's supposed to do anything else
	BPhero_UWB_Message_Init();

	uint8_t token = 0;

	msg_f_send.sourceAddr[0] = 1 & 0xFF;		// copy the address
	msg_f_send.sourceAddr[1] = (1 >> 8) & 0xFF; // copy the address

	while (1)
	{
		msg_f_send.destAddr[0] = (2 + token) & 0xFF;
		msg_f_send.destAddr[1] = ((2 + token) >> 8) & 0xFF;

		msg_f_send.seqNum = distance_seqnum;
		msg_f_send.messageData[0] = 'P'; // Send Poll Message

		radio->dwt_writetxdata(psduLength + 1, (uint8_t *)&msg_f_send, 0);

		radio->dwt_writetxfctrl(psduLength + 1, 0, 1);
		// Set the time to start sending to the receiver.
		radio->dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
		// Set the timeout duration.
		radio->dwt_setrxtimeout(300);
		// Set the preamble timeout.
		radio->dwt_setpreambledetecttimeout(0);
		// Send immediately upon startup
		radio->dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

		// Waiting for reception to complete
		while (!((status_reg = radio->dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR)))
		{
		};

		// auto status_reg = 0;
		// do {
		// 	status_reg = radio->check_for_rx();
		// } while(!status_reg);


		if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
		{
			//frame_len = radio->dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
			frame_len = radio->get_frame_length();

			// cur_ts = get_cur_timestamp_u64();

			radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_TXFRS_BIT_MASK);


			if (frame_len <= FRAME_LEN_MAX)
			{
				// Read the transmitted data.
				radio->dwt_readrxdata(rx_buffer, frame_len, 0);
				// Convert the transmitted data into message format.
				msg_f_recv = (srd_msg_dsss *)rx_buffer;
			}

			if ('A' == msg_f_recv->messageData[0])
			{

				// Read the Poll transmission time and the Resp reception time.
				poll_tx_ts = radio->get_tx_timestamp_u64();
				resp_rx_ts = radio->get_rx_timestamp_u64();

				// Set the final delayed sending time.
				final_tx_time = (resp_rx_ts + ((RESP_RX_TO_FINAL_TX_DLY_UUS)*UUS_TO_DWT_TIME)) >> 8;

				radio->dwt_setdelayedtrxtime(final_tx_time);

				final_tx_ts = (((uint64_t)(final_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

				// Configure Final Data Transmission
				msg_f_send.messageData[0] = 'F'; // Final message
				final_msg_set_ts(&msg_f_send.messageData[FINAL_MSG_POLL_TX_TS_IDX], poll_tx_ts);
				final_msg_set_ts(&msg_f_send.messageData[FINAL_MSG_RESP_RX_TS_IDX], resp_rx_ts);
				final_msg_set_ts(&msg_f_send.messageData[FINAL_MSG_FINAL_TX_TS_IDX], final_tx_ts);
				//final_msg_set_ts(&msg_f_send.messageData[FINAL_MSG_FINAL_TX_TS_IDX + 4], final_tx_ts); //what was this one for?

				radio->dwt_writetxdata(9 + 17, (uint8_t *)&msg_f_send, 0); // write the frame data
				radio->dwt_writetxfctrl(9 + 17, 0, 1);
				// Delayed sending

				// cur_ts = get_cur_timestamp_u64();

				ret = radio->dwt_starttx(DWT_START_TX_DELAYED);

				// dwt_readdiagnostics(&rx_diag1);
				// uint16_t fp_int1 = rx_diag1.firstPath >> 6;
				//uint16_t fp_int1 = radio->dwt_read16bitoffsetreg(RX_TIME_ID, RX_TIME_FP_INDEX_OFFSET) >> 6;
				//radio->dwt_readaccdata(cir_buffer1, CIR_LENGTH * 4, (fp_int1) * 4);

				dwt_rxdiag_t diagnostics = {};
       			radio->dwt_readdiagnostics(&diagnostics);
				uint16_t fp_index = diagnostics.ipatovFpIndex >> 6; //bit shifting removes the fractional part
				radio->dwt_readaccdata(cir_buffer1, (COMPLEX_BYTE_LEN * CIR_LEN + 1), fp_index);


				if (DWT_SUCCESS == ret)
				{

					msg_f_send.messageData[0] = 'X'; // Final message

					for (int i = 0; i < CIR_LENGTH * 4; i++)
					{
						// Requires the use of the FP's CIR.
						//msg_f_send.messageData[FINAL_MSG_POLL_TX_TS_IDX + i] = cir_buffer1[i + 5];

						msg_f_send.messageData[FINAL_MSG_POLL_TX_TS_IDX + i] = cir_buffer1[i + 1];
					}

					/* Write and send final2 message. */
					final2_tx_time = (resp_rx_ts + ((540 * 2) * UUS_TO_DWT_TIME)) >> 8;

					while (!((status_reg = radio->dwt_read32bitreg(SYS_STATUS_ID)) & SYS_STATUS_TXFRS_BIT_MASK))
					{
					};

					radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);

					radio->dwt_setdelayedtrxtime(final2_tx_time);

					radio->dwt_writetxdata(psduLength + 16, (uint8_t *)&msg_f_send, 0); // write the frame data
					radio->dwt_writetxfctrl(psduLength + 16, 0, 1);

					ret = radio->dwt_starttx(DWT_START_TX_DELAYED);

					while (!(radio->dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK))
					{
					};

					/* Clear TX frame sent event. */
					radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);

					Final_Distance = (msg_f_recv->messageData[1] * 100 + msg_f_recv->messageData[2]); // cm

					float uwb_rssi = 0;
					// uwb_rssi = dwGetReceivePower();

					//										if (msg_f_send.seqNum % 3 == 2)
					//										{
					//												token = (token + 1) % board_num;
					//										}

					token = (token + 1) % board_num;

					//										if (msg_f_send.seqNum % 3 == 2)
					//										{
					//												//HalDelay_nMs();
					//										}

					// Increment Sequence Num
					if (distance_seqnum == 254)
					{
						distance_seqnum = 0;
					}
					else
					{
						distance_seqnum++;
					}
					//++distance_seqnum;
				}
				else
				{
					// OLED_ShowString(0,0,"Final Fail");
				}
			}
		}
		else
		{

			// OLED_ShowString(0,1,"Resp Fail");
			radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
		}
	}

	//return 0;
}

// #else
#endif

#ifdef RX_NODE

static srd_msg_dsss *msg_f;
static double tof;
static double distance;

// Define the save timestamp.
static uint32_t poll_tx_ts, resp_rx_ts, final_tx_ts;
static uint32_t poll_rx_ts_32, resp_tx_ts_32, final_rx_ts_32;

static uint64_t poll_rx_ts;
static uint64_t resp_tx_ts;
static uint64_t final_rx_ts;
static uint64_t final2_rx_ts;

static double Ra, Rb, Da, Db;
static int64 tof_dtu;
static int temp = 0;	   // Save temporary variable
static float uwb_rssi = 0; // Define a variable to store the RSSI signal strength.

static int n = 0;

static uint8_t debug = 0;

static uint8_t uCurrentTrim_val = 19;

static uint16_t addr = 2;

int dw_main(void)
{

	// RX_ANT_DLY = rfDelaysTREK[1];
	// TX_ANT_DLY = rfDelaysTREK[1];

	// Modify it to use our own code.
	reset_DW1000();

	SPI_ConfigFastRate(SPI_BaudRatePrescaler_32);

	// Initialize the DW1000
	if (dwt_initialise(DWT_LOADUCODE) == DWT_ERROR)
	{
		while (1)
		{
		};
	}

	// Read crystal oscillator calibration parameters.
	// dwt_xtaltrim(uCurrentTrim_val);

	// dwt_readfromdevice(FS_CTRL_ID,FS_XTALT_OFFSET,1,&uCurrentTrim_val);
	// uCurrentTrim_val &= 31;

	// Adjust the SPI to 18 MHz.
	SPI_ConfigFastRate(SPI_BaudRatePrescaler_4);

	// Configure the operating frequency
	// config.txCode = 7 + addr;
	// config.rxCode = 7 + addr;
	radio->dwt_configure(&config);
	// Activate the DW1000 status indicator.
	radio->dwt_setleds(1);
	// Configure transmit power
	radio->dwt_configuretxrf(&txconfig2);
	// Set Work Network ID
	radio->dwt_setpanid(NET_PANID);
	// Set own short address
	radio->dwt_setaddress16(addr);
	// Configure antenna delay
	radio->dwt_setrxaftertxdelay(RX_ANT_DLY);
	radio->dwt_settxantennadelay(TX_ANT_DLY);

	// Configuring the DW1000 interrupt, although it is not actually used.
	//dwt_setinterrupt(DWT_INT_RFCG | (DWT_INT_ARFE | DWT_INT_RFSL | DWT_INT_SFDT | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFTO /*| DWT_INT_RXPTO*/), 1);

	BPhero_UWB_Message_Init();

	uint8_t err_cnt = 0;

	msg_f_send.sourceAddr[0] = (2 + addr) & 0xFF;		 // copy the address
	msg_f_send.sourceAddr[1] = ((2 + addr) >> 8) & 0xFF; // copy the address

	uint16_t counter = 0;

	while (1)
	{

		// Start receiving
		// Step1:Enable frame filtering --> Receive data packets only; for more information on frame filtering features, please refer to 51uwb.cn.
		// dwt_enableframefilter(DWT_FF_DATA_EN);
		// Step2:Set the reception timeout; a timeout parameter of 0 indicates that the system remains in the receiving state indefinitely.
		radio->dwt_setrxtimeout(1000);
		radio->dwt_setpreambledetecttimeout(0);
		// Step3:Initiate reception immediately. The parameters of this function allow for a delayed start to reception; this can be optimized by delaying the receiver's activation to reduce energy consumption.
		radio->dwt_rxenable(DWT_START_TX_IMMEDIATE);

		while (!((status_reg = radio->dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR)))
		{
		};

		// Determine whether the complete data has been received.
		if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
		{
			if (err_cnt > 0)
			{
				err_cnt = 0;
			}
			// Read the length of the received data.
			frame_len = radio->get_frame_length(); //radio->dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;

			radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

			// If the data length is less than the maximum value, it is considered reasonable.
			if (frame_len <= FRAME_LEN_MAX)
			{

				// Read the received data.
				radio->dwt_readrxdata(rx_buffer, frame_len, 0);
				// Force the data into the agreed-upon format.
				msg_f = (srd_msg_dsss *)rx_buffer;

				if ((msg_f->destAddr[0] != msg_f_send.sourceAddr[0]) || (msg_f->destAddr[1] != msg_f_send.sourceAddr[1]))
				{
					continue;
				}

				// Extract the short address used to send this data and assign it to the destination address for the message to be sent.
				// Where did the information come from, and who should the reply be sent to?
				msg_f_send.destAddr[0] = msg_f->sourceAddr[0];
				msg_f_send.destAddr[1] = msg_f->sourceAddr[1];
				// Extract sequence number
				msg_f_send.seqNum = msg_f->seqNum;

				uint16_t fp_int1 = radio->dwt_read16bitoffsetreg(RX_TIME_ID, RX_TIME_FP_INDEX_OFFSET) >> 6;

				//							if (fp_int1 > 757 || fp_int1 < 730)
				//							{
				////								uint16_t firstPathAmp2 = dwt_read16bitoffsetreg(RX_FQUAL_ID, 0x2);
				////								n += sprintf((char *)&usbVCOMout[n], "f, %0x, %d, %d\r\n", msg_f_send.seqNum, fp_int1, firstPathAmp2);

				////								HalUsbWrite(usbVCOMout, n);
				////								n = 0;
				//								n = 0;
				//								n += sprintf((char *)&usbVCOMout[n], "error\r");
				//								HalUsbWrite(usbVCOMout, n);
				//
				//								n = 0;
				//								// continue;
				//							}

				if ('P' == msg_f->messageData[0])
				{

					uint32_t resp_tx_time;
					int ret;

					// Save the timestamp of when this message was received.
					poll_rx_ts = radio->get_rx_timestamp_u64();

					resp_tx_time = (poll_rx_ts + ((POLL_RX_TO_RESP_TX_DLY_UUS)*UUS_TO_DWT_TIME)) >> 8;
					radio->dwt_setdelayedtrxtime(resp_tx_time);

					radio->dwt_setrxaftertxdelay(RESP_TX_TO_FINAL_RX_DLY_UUS);
					radio->dwt_setrxtimeout(500);

					msg_f_send.messageData[0] = 'A'; // Poll ack message
					// Package and send the previous ranging information.
					temp = (int)(distance * 100); // convert m to cm
					msg_f_send.messageData[1] = temp / 100;
					msg_f_send.messageData[2] = temp % 100;
					// Write the data to be sent into the UWB registers.
					radio->dwt_writetxdata(psduLength + 3, (uint8_t *)&msg_f_send, 0); // write the frame data
					// Indicate that the UWB data transmission offset is 0.
					radio->dwt_writetxfctrl(psduLength + 3, 0, 1);

					// Send immediately upon startup
					ret = radio->dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
					// Waiting for transmission to complete

					// uint16_t fp_int1 = dwt_read16bitoffsetreg(RX_TIME_ID, RX_TIME_FP_INDEX_OFFSET) >> 6;
					radio->dwt_readaccdata(cir_buffer1, CIR_LENGTH * 4, (fp_int1) * 4);

					// MUST WAIT!!!!!
					while (!((status_reg = radio->dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR)))
					{
					};

					if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
					{

						radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

						frame_len = radio->get_frame_length(); //radio->dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;

						// 60us
						if (frame_len <= FRAME_LEN_MAX)
						{

							// Read the received data.
							radio->dwt_readrxdata(rx_buffer, frame_len, 0);
							// Force the data into the agreed-upon format.
							msg_f = (srd_msg_dsss *)rx_buffer;

							// Extract the short address used to send this data and assign it to the destination address for the message to be sent.
							// Where did the information come from, and who should the reply be sent to?
							msg_f_send.destAddr[0] = msg_f->sourceAddr[0];
							msg_f_send.destAddr[1] = msg_f->sourceAddr[1];
							// Extract sequence number
							//  80us
							msg_f_send.seqNum = msg_f->seqNum;

							if ('F' == msg_f->messageData[0])
							{
								// printf("Receive Final\r");
								// Save the timestamp of sending message A.
								resp_tx_ts = radio->get_tx_timestamp_u64();
								// Save the timestamp of receiving the 'F' message.
								final_rx_ts = radio->get_rx_timestamp_u64();

								// Extract timestamp information from the data packet payload.
								//  50us
								final_msg_get_ts(&msg_f->messageData[FINAL_MSG_POLL_TX_TS_IDX], &poll_tx_ts);
								final_msg_get_ts(&msg_f->messageData[FINAL_MSG_RESP_RX_TS_IDX], &resp_rx_ts);
								final_msg_get_ts(&msg_f->messageData[FINAL_MSG_FINAL_TX_TS_IDX], &final_tx_ts);

								// dwt_readdiagnostics(&rx_diag2);
								uint16_t fp_int2 = radio->dwt_read16bitoffsetreg(RX_TIME_ID, RX_TIME_FP_INDEX_OFFSET) >> 6;
								// uint16_t fp_int2 = rx_diag2.firstPath >> 6;
								radio->dwt_readaccdata(cir_buffer2, CIR_LENGTH * 4, (fp_int2) * 4);
								// dwt_readaccdata(cir_buffer2, 4 * CIR_LENGTH, (fp_int2-6) * 4);

								uint32_t final2_rx_enable = (final_rx_ts + (430 * UUS_TO_DWT_TIME)) >> 8;

								radio->dwt_setdelayedtrxtime(final2_rx_enable);
								radio->dwt_setrxtimeout(500);

								radio->dwt_rxenable(1);

								/* Poll for reception of a frame or error/timeout. See NOTE 7 below. */
								while (!((status_reg = radio->dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR)))
								{
								};

								if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
								{

									final2_rx_ts = radio->get_rx_timestamp_u64();
									frame_len = radio->get_frame_length(); //radio->dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;

									radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

									for (uint32_t i = 0; i < FRAME_LEN_MAX; i++)
									{
										rx_buffer[i] = '\0';
									}

									if (frame_len <= FRAME_LEN_MAX)
									{
										// Read the received data.
										radio->dwt_readrxdata(rx_buffer, frame_len, 0);

										// Force the data into the agreed-upon format.
										msg_f = (srd_msg_dsss *)rx_buffer;
										// Extract the short address used to send this data and assign it to the destination address for the message to be sent.
										// Where did the information come from, and who should the reply be sent to?
										msg_f_send.destAddr[0] = msg_f->sourceAddr[0];
										msg_f_send.destAddr[1] = msg_f->sourceAddr[1];
										// Extract sequence number
										msg_f_send.seqNum = msg_f->seqNum;

										if ('X' == msg_f->messageData[0])
										{

											for (int i = 0; i < CIR_LENGTH * 4; i++)
											{
												cir_buffer3[i] = msg_f->messageData[FINAL_MSG_POLL_TX_TS_IDX + i];
											}

											// dwt_readdiagnostics(&rx_diag4);
											// uint16_t fp_int4 = rx_diag4.firstPath >> 6;
											uint16_t fp_int4 = radio->dwt_read16bitoffsetreg(RX_TIME_ID, RX_TIME_FP_INDEX_OFFSET);
											radio->dwt_readaccdata(cir_buffer4, CIR_LENGTH * 4, ((fp_int4 >> 6)) * 4);

											// For distance calculation based on the TWR algorithm, you can refer to the video tutorial on 51uwb.cn.
											poll_rx_ts_32 = (uint32_t)poll_rx_ts;
											resp_tx_ts_32 = (uint32_t)resp_tx_ts;
											final_rx_ts_32 = (uint32_t)final_rx_ts;
											Ra = (double)(resp_rx_ts - poll_tx_ts);
											Rb = (double)(final_rx_ts_32 - resp_tx_ts_32);
											Da = (double)(final_tx_ts - resp_rx_ts);
											Db = (double)(resp_tx_ts_32 - poll_rx_ts_32);
											tof_dtu = (int64)((Ra * Rb - Da * Db) / (Ra + Rb + Da + Db));

											tof = tof_dtu * DWT_TIME_UNITS;
											distance = tof * SPEED_OF_LIGHT;
											// The official documentation provides offset correction data, allowing users to adjust the offset table values ​​as appropriate based on the environment.
											// distance = distance - dwt_getrangebias(config.chan,(float)distance, config.prf);//Distance minus correction factor
											// Apply Kalman filtering to the calculated distance.
											// kalman filter
											// distance = KalMan(distance);

											int32_t ci;
											float clockOffsetHertz;
											float clockOffsetPPM;

											ci = radio->dwt_readcarrierintegrator();

											clockOffsetHertz = ci * FREQ_OFFSET_MULTIPLIER;

											if (msg_f_send.seqNum % 3 == 0)
											{
												clockOffsetPPM = clockOffsetHertz * HERTZ_TO_PPM_MULTIPLIER_CHAN_5;
											}
											else if (msg_f_send.seqNum % 3 == 1)
											{
												clockOffsetPPM = clockOffsetHertz * HERTZ_TO_PPM_MULTIPLIER_CHAN_9;
											}
											else if (msg_f_send.seqNum % 3 == 2)
											{
												clockOffsetPPM = clockOffsetHertz * HERTZ_TO_PPM_MULTIPLIER_CHAN_5;
											}

											//																				if (clockOffsetPPM > 1.3f)
											//																				{
											//																						if (uCurrentTrim_val >= 1 && uCurrentTrim_val <= 31)
											//																						{
											//
											//																							SPI_ConfigFastRate(SPI_BaudRatePrescaler_32);
											//
											//																							uCurrentTrim_val -= 1;
											//																							dwt_xtaltrim(uCurrentTrim_val);
											//																							SPI_ConfigFastRate(SPI_BaudRatePrescaler_4);
											//
											//																						}
											//																				}
											//																				else if(clockOffsetPPM < -1.3f)
											//																				{
											//																						if (uCurrentTrim_val >= 1 && uCurrentTrim_val <= 31)
											//																						{
											//
											//																							SPI_ConfigFastRate(SPI_BaudRatePrescaler_32);
											//
											//																							uCurrentTrim_val += 1;
											//																							dwt_xtaltrim(uCurrentTrim_val);
											//																							SPI_ConfigFastRate(SPI_BaudRatePrescaler_4);
											//
											//																						}
											//																				}

											// Send distance information to the serial port.
											// printf("0x%04X <--> 0x%02X%02X :%d cm\r\n",SHORT_ADDR,msg_f_send.destAddr[1],msg_f_send.destAddr[0],(int)(100*distance));
											temp = (int)(distance * 100);

											//don't strictly need this, so we'll ignore it.
											uwb_rssi = 0;
											//uwb_rssi = dwGetReceivePower();

											struct cir_tap_struct *cir = (struct cir_tap_struct *)&cir_buffer1[1];

											for (int j = 1; j < 2; j++)
											{
												n += sprintf((char *)&usbVCOMout[n], "%04x,%04x,", cir[j].real, cir[j].imag);
											}

											n += sprintf((char *)&usbVCOMout[n], "%04x,%04x,%02x,%d\r", 0, 1, msg_f_send.seqNum, fp_int1);

											cir = (struct cir_tap_struct *)&cir_buffer2[1];
											;

											for (int j = 1; j < 2; j++)
											{
												n += sprintf((char *)&usbVCOMout[n], "%04x,%04x,", cir[j].real, cir[j].imag);
											}

											n += sprintf((char *)&usbVCOMout[n], "%04x,%04x,%02x,%f\r", 0, 2, msg_f_send.seqNum, clockOffsetPPM);

											cir = (struct cir_tap_struct *)cir_buffer3;

											for (int j = 0; j < 1; j++)
											{
												n += sprintf((char *)&usbVCOMout[n], "%04x,%04x,", cir[j].real, cir[j].imag);
											}

											n += sprintf((char *)&usbVCOMout[n], "%04d,%04x,%02x,%6.5f\r", uCurrentTrim_val, 3, msg_f_send.seqNum, uwb_rssi);

											cir = (struct cir_tap_struct *)&cir_buffer4[1];
											;

											for (int j = 1; j < 2; j++)
											{
												n += sprintf((char *)&usbVCOMout[n], "%04x,%04x,", cir[j].real, cir[j].imag);
											}

											n += sprintf((char *)&usbVCOMout[n], "%04x,%04x,%02x,%6.5f\r~", fp_int4, 4, msg_f_send.seqNum, distance);

											// App_Module_Uart_USB_Send(usbVCOMout, n);

											// printf("%.*s", n, usbVCOMout);
											//HalUsbWrite(usbVCOMout, n);
											Serial.write(usbVCOMout, n);

											n = 0;
										}
									}
								}
								else
								{
									/* Clear RX error events in the DW1000 status register. */
									radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
								}
								// freq_hooping(msg_f_send.seqNum, addr-2);
								// freq_hooping(msg_f_send.seqNum, 0);
								// if (msg_f_send.seqNum % 3 == 2)
								// dwt_forcetrxoff();
								// HalDelay_nMs(7);
							}
						}
					}
					else
					{
						/* Clear RX error events in the DW1000 status register. */
						radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
					}
				}
			}
		}
		else
		{
			/* Clear RX error events in the DW1000 status register. */
			radio->dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);

			//					if(msg_f_send.seqNum % 3 != 2){
			//
			//						err_cnt++;
			//						if (err_cnt >= 2)
			//						{
			//
			//							freq_hooping(msg_f_send.seqNum,addr-2);
			//							err_cnt = 0;
			//
			//							n += sprintf((char *)&usbVCOMout[n], "change, %d\r\n", msg_f_send.seqNum);

			//							HalUsbWrite(usbVCOMout, n);
			//							n = 0;
			//						}
			//					}
		}
	}

	return 0;
}

#endif

// #endif
