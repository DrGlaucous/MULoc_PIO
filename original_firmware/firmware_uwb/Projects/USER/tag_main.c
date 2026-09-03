/*! ------------------------------------------------------------------------------------------------------------------
 * @file tag_main.c
 * @brief Tag scheduling main loop. The UWB tag keeps on overhearing UWB packets sent by anchors and send TO/AO estimates
 * to the PC over UWB.
 *
 * @author Junqi Ma
 * @date 2024.7.13
 *--------------------------------------------------------------------------------------------------------------------
 */

 /*Includes*/
#include "compiler.h"
#include "port.h"

#include "deca_types.h"
#include "deca_regs.h"

#include "deca_spi.h"
#include "dw_main.h"
#include "dwm1000_timestamp.h"

#include "hal_led.h"
#include "hal_usb.h"
#include "hal_spi.h"
#include "hal_usart.h"

#include "bphero_uwb.h"

#include <math.h>

#ifdef RX_NODE

extern void usb_run(void);
extern int usb_init(void);

static int ret;

static uint32 status_reg = 0;

#define LCD_BUFF_LEN (200)
static uint8 usbVCOMout[LCD_BUFF_LEN];

//configs found in bphero
extern dwt_config_t config;
extern dwt_config_t config2;

extern dwt_txconfig_t txconfig2;
extern dwt_txconfig_t txconfig3;


//only used by the anchors, is the header that preprends all anchor packets
static uint8_t msg_common[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21};

//Channel Impulse Response = CIR
static uint8_t cir_buffer[(4 * CIR_LEN + 1) * ANCHOR_NUM];

//rx timestamp
static uint64_t rx_ts[ANCHOR_NUM];


/* Payload format (CIR and receiving timestamps of messages from other anchors)

|---CIR(4 byte)---|--RxTime(5 byte)--|******|---CIR(4 byte)---|--RxTime(5 byte)--|--TxTime(5 byte)--|-IDX(1 byte)-|
|---------------------------13 byte * (Anchor Number-1)--------------------------|--------------6 byte------------| 
*/
//13 bytes total
//[2 bytes ][2 bytes][1 byte          ][1 byte               ][2 bytes       ][5 bytes]
//[CIR real][CIR img][phase correction][preamble accumulation][max growth cir][rx time]


//this is a direct representation of the output files in the matlab folders. Each msg_buffer is printed wholesale, per line
static uint8_t msg_buffer[((ANCHOR_NUM - 1) * SINGLE_LEN + END_LEN) * ANCHOR_NUM];



/* Frame sequence number, incremented after each transmission. */
static uint8_t frame_seq_nb = 0;

static int n = 0;

//first path index
static uint16 fp_index;

//which anchor we're getting the data from
static uint16_t current_tx;


//used for indexing into the input buffer to get the data from each anchor about the other anchors.
//offset1 represents a line in the result CSV, from the parent anchor
static uint16_t offset1;
//offset2 represents a single anchor (relative to the offset1 anchor) in the result CSV
static uint16_t offset2;

static uint16_t current_freq = 1;

static int32 ci[ANCHOR_NUM];

/* Phase compenstation value measured using SFD */
uint8_t phase_cal[ANCHOR_NUM];

/* RSSI value for timestamp compensation */
uint16_t maxGC[ANCHOR_NUM];
uint8_t rxPC[ANCHOR_NUM];

void dw_init(void)
{
	reset_DW1000();

	// Config the SPI speed to 2 MHz
	SPI_ConfigFastRate(SPI_BaudRatePrescaler_32);

	// DW1000 Initialization
	if (dwt_initialise(DWT_LOADUCODE) == DWT_ERROR)
	{
		while (1)
		{
		};
	}

	//load in one-time-programmable settings
	dwt_loadopsettabfromotp(DWT_OPSET_TIGHT);

	// Config the SPI speed to 18 MHz
	SPI_ConfigFastRate(SPI_BaudRatePrescaler_4);

	// Config the RF parameters
	dwt_configure(&config); //generic config stuff like channel, preamble size, datarate, etc.
	dwt_configuretxrf(&txconfig2); //TX power and PG delay

	// Enable the DW1000 TX/RX indicator LED
	dwt_setleds(1);

	// Configure network ID
	dwt_setpanid(NET_PANID);

	// Configure the 16bit short address
	dwt_setaddress16(1);

	// Configure the TX and RX antenna delay, this program puts all the delay on the TX side.
	dwt_setrxantennadelay(RX_ANT_DLY);
	dwt_settxantennadelay(TX_ANT_DLY);
}

int dw_main(void)
{

	// Initialization
	dw_init();

	// The index of anchor that is currently sending message
	uint8_t current_idx = 0;


	// True or False
	//unused by the tag
	//uint8_t is_last_anchor = 0;
	//uint8_t ret = 0;

	// The main loop
	while (1)
	{

		//disable preamble timeout
		dwt_setpreambledetecttimeout(0);

		/* Clear reception timeout to start next ranging process. */
		//There is no RX timeout for the tags
		dwt_setrxtimeout(0);

		/* Activate reception immediately. */
		dwt_rxenable(DWT_START_RX_IMMEDIATE);

		// Waiting for reception completion
		while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_ERR)))
		{};

		//good packet
		if (status_reg & SYS_STATUS_RXFCG)
		{

			// Read the frame length from DW register
			frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;

			//clear status
			dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG | SYS_STATUS_TXFRS);

			//pull in fresh data
			if (frame_len <= FRAME_LEN_MAX)
			{
				// Read data from RX buffer
				dwt_readrxdata(rx_buffer, frame_len, 0);
			}


			//the number of the anchor/tag sending stuff to us
			current_tx = rx_buffer[8];

			//get frame sequence number from the buffer
			frame_seq_nb = rx_buffer[ALL_MSG_SN_IDX];

			// Copy the AO data from rx_buffer to msg_buffer
			//AO = Anchor... something? (offset?)
			memcpy(
				msg_buffer +
				((ANCHOR_NUM - 1) * SINGLE_LEN + END_LEN) * current_tx, //offset forward by whichever anchor this belongs to
				rx_buffer + ALL_MSG_COMMON_LEN, //skip past the header
				(ANCHOR_NUM - 1) * SINGLE_LEN //a single slot's worth of data (why does it change with anchor count?)
				+ END_LEN //actually not sure why this isn't part of the header (even though it's 0).
			);

			//stow rx timestamp when we got this packet
			rx_ts[current_tx] = get_rx_timestamp_u64();

			// Read the first path channel impulse response and save it to local buffer
			//for the DW3000, this can be gotten from dwt_readdiagnostics and ipatovPeak
			fp_index = dwt_read16bitoffsetreg(RX_TIME_ID, RX_TIME_FP_INDEX_OFFSET) >> 6;

			//use that to read accumulator data (page 228 of the DW3000 manual, even though this is the DW1000. I want to port this stuff to the DW3000)
			dwt_readaccdata(cir_buffer + current_tx * (CIR_LEN * 4 + 1),
				CIR_LEN * 4 + 1, //13 bytes. Why is this 13 bytes?
				(fp_index) * 4 //offset it to where the CIR is kept (not sure why the *4 though)
			);


			// Read Carrier integer for clock drift estimation
			ci[current_tx] = dwt_readcarrierintegrator();

			// Read RCPHASE for phase compensation into its corresponding anchor slot
			//todo: find the DW3000 version of this
			dwt_readfromdevice(RX_TTCKO_ID, 4, 1, phase_cal + current_tx);

			// Read max growth cir and rxPC for RSSI estimation
			//for the DW3000, found using dwt_readdiagnostics under ipatovF1-6 (?)
			maxGC[current_tx] = dwt_read16bitoffsetreg(RX_FQUAL_ID, 0x6);

			//read rxpacc from RX_FINFO - Preamble Accumulation Count, see page 97 of the DW1000 manual
			//it is in the high 3 nibbles of the RX_FINFO register. We only keep the lower 2 nibbles in rxPC
			rxPC[current_tx] = (dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXPACC_MASK) >> RX_FINFO_RXPACC_SHIFT;

			// Ranging finished, send the ranging data to SoC/PC over USB
			//copy all of the ranging info into usbVCOMout to be sent out
			//we send raw serial data out to the receiver. the python script `utils.py` is what turns this back into ASCII values.

			//if the current anchor we got is the last one
			if (ANCHOR_NUM == current_tx + 1)
			{
				//this is a weird and hard-to-understand way to copy the data.
				//why did they do it this way?

				int xx = 0;

				//anchor... uh.... label your acronyms, dude.

				// AO estimates acquired from anchor i
				for (uint8_t i = 0; i < ANCHOR_NUM; i++)
				{

					//big anchor count (each "big" anchor contains info about the other ones it ranged from)
					//this offset represents each line in the CSV file generated by `byte2txt` in utils.py
					offset1 = ((ANCHOR_NUM - 1) * SINGLE_LEN + END_LEN) * i;

					// AO estimates of anchor j acquired from anchor i
					for (uint8_t j = 0; j < ANCHOR_NUM; j++)
					{
						uint64_t rx_time;

						if (j < i)
						{
							offset2 = j * SINGLE_LEN;
						}
						else if (j > i)
						{
							offset2 = (j - 1) * SINGLE_LEN;
						}
						//is our anchor, skip it
						else if (j == i)
						{
							continue;
						}

						/* 4 byte CIR + 1 byte RPHASE + 1 byte rxPC + 2 byte maxGC + 5 byte rx_time */
						//13 bytes total, 
						//[CIR real][CIR img][phase correction][preamble accumulation][max growth cir][rx time]
						memcpy(usbVCOMout + n,
							msg_buffer + offset1 + offset2, SINGLE_LEN);
						n += SINGLE_LEN;
					}

					usbVCOMout[n] = frame_seq_nb;
					n += 1;
				}

				// Then TO estimates (from us, the tag)
				//tag data, gleaned from reading the incoming radio values above, one for each anchor
				for (uint8_t i = 0; i < ANCHOR_NUM; i++)
				{

					//chan. impulse response CIR
					memcpy(usbVCOMout + n, cir_buffer + (4 * CIR_LEN + 1) * i + 1 + 4, 4);
					n += 4;

					// Phase correction value
					usbVCOMout[n] = phase_cal[i];
					n += 1;

					usbVCOMout[n] = rxPC[i];
					n += 1;

					usbVCOMout[n] = (uint8_t)(maxGC[i] >> 8);
					usbVCOMout[n + 1] = (uint8_t)(maxGC[i]);
					n += 2;

					// Receiving timestamps, do conversions from u64 to char array
					for (int k = 0; k < 5; k++)
					{
						usbVCOMout[n] = (uint8_t)(rx_ts[i] >> ((4 - k) * 8));
						n += 1;
					}

					// Carrier integer number, do conversion from int to char array
					for (int k = 0; k < 4; k++)
					{
						usbVCOMout[n] = (uint8_t)(ci[i] >> ((3 - k) * 8));
						n += 1;
					}
				}

				// Frame sequence number
				usbVCOMout[n] = frame_seq_nb;
				n += 1;

				// Used for data segmentation: 7,6,5,4,3
				for (int i = 0; i < 5; i++)
				{
					usbVCOMout[n] = 7 - i;
					n += 1;
				}

				HalUsbWrite(usbVCOMout, n);
				n = 0;

				// Perform frequency hopping every two rounds of localization
				if (frame_seq_nb % 2 == 1)
				{
					if (frame_seq_nb % 4 == 1)
					{
						
						// Switch to channel 3
						current_freq = 3;

						dwt_forcetrxoff();
						dwt_configure(&config2);
						dwt_configuretxrf(&txconfig3);
					}
					else if (frame_seq_nb % 4 == 3)
					{

						// Switch to channel 1
						current_freq = 1;

						dwt_forcetrxoff();
						dwt_configure(&config);
						dwt_configuretxrf(&txconfig2);
					}
				}
			}
		}
		//bad packet, clear errors and try again
		else
		{
			dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
		}
	}
}

#endif
