#include <Arduino.h>
#include "constants.h"
#include "dw3000.h"
#include "dw3000_regs.h"
#include "dw3000_shared_defines.h"
#include "SPI.h"
#include "packet.h"


//we need to calibrate the board in order to get these
#define RX_ANT_DELAY 16385
#define TX_ANT_DELAY 16385

//it's up to the MC to accept or reject packets based on this.
#define RESPONDER_MAC 0x1122334455667788
#define SOURCE_MAC 0x8877887788778877

DummyStream* dummy = nullptr;
DWUart* uart = nullptr;
DW3000Port* port = nullptr;
DW3000* radio = nullptr;

//how the radio should be configured for the session.
static const dwt_config_t config = {
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
    DWT_STS_MODE_1 | DWT_STS_MODE_SDC, 		/* STS disabled */
    DWT_STS_LEN_64,   		/* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       		/* PDOA mode off */
};

//I'll keep both of these here for reference.
dwt_txconfig_t txconfig_ch5 =
{
    0x34,           /* PG delay. */
    0xfdfdfdfd,      /* TX power. */
    0x0             /*PG count*/
};

dwt_txconfig_t txconfig_ch9 =
{
    0x34,           /* PG delay. */
    0xfefefefe,     /* TX power. */
    0x0             /*PG count*/
};


bool flip = false;


/////////////////////helper functions

//platform specific code, this will need to be changed for non-NRF devices!
//MAC address should be hard-coded into each device
uint64_t get_uuid() {

	//collect both halves of the unique device ID
	uint64_t lsb = (uint64_t)NRF_FICR->DEVICEID[0];
	uint64_t msb = (uint64_t)NRF_FICR->DEVICEID[1];

	//merge them together and return them as one chunk
	return (msb << 32) | lsb;
}

//prints a u64, since the arduino IDE's serial.print doesn't handle this
void print_u64(uint64_t value) {
	Serial.print((uint32_t)(value >> 32));
	Serial.print((uint32_t)(value & 0xFFFFFFFF));
}

//writes a UWBPacket out to the radio and waits for it to respond with the send status
//if switch_to_rx is true, the radio will immediately go into RX mode after transmission (a reset fast command needs to be sent to get out of this mode)
bool send_packet(UWBPacket& packet, uint8_t mode) {
	
	//write actual data to the outgoing buffer, automatically accounts for >127 packet sizes
	radio->dwt_writetxdata(packet.get_compiled_len(), packet.get_compiled(), 0);

	//append FC data (2 byte checksum), identify this packet as a ranging packet
	radio->dwt_writetxfctrl(packet.get_compiled_len() + FCS_LEN, 0, 1);

	//start TX mode
	int first_send_error = radio->dwt_starttx(mode);

	if(first_send_error != DWT_SUCCESS) {
		Serial.print("Delay send error: ");
		Serial.println(first_send_error);
		return false;
	}

	//check for a successful transmit with timeout
	bool send_error = true;
	for(int i = 0; i < 50; ++i) {
		if(radio->check_frame_tx_success()) {
			send_error = false;
			break;
		}
	}

	//had problem sending packet, reset radio status and return
	if(send_error) {
		radio->clear_system_status();
		radio->dwt_writefastCMD(CMD_TXRXOFF);
		Serial.println("Send Error");
		return false;
	}

	return true;

}

//reads the last gotten data as a UWB packet out of the radio's internal buffer
UWBPacket get_packet() {


	//parse the packet to get the timestamps from the other radio
	//get size of frame and read it in (minus CRC)
	uint32_t frame_length = radio->get_frame_length() - FCS_LEN;
	uint8_t frame_data[frame_length] = {};
	radio->dwt_readrxdata(frame_data, frame_length, 0);
	return UWBPacket(frame_data, frame_length);

}

//blocks until the radio gets a message in or until timeout_ms is reached.
//the radio must already be set to the correct mode with a fast command!
//returns 0 on success, 1 on timeout, -1 on error
int wait_for_message_with_timeout(uint32_t timeout_ms, bool no_timeout = false) {


	bool got_response = false;
	bool error = false;
	auto tx_time = millis();
	while(no_timeout || (millis() - tx_time < timeout_ms)) {
		//bool error = false;
		auto response = radio->check_for_rx();
		switch(response) {

			case 1: //got correct packet
			{
				got_response = true;
				break;
			}
			case 2:
			case 3: //some error, we got a packet, but we need to try again; it was corrupted
			{
				error = true;
				break;
			}
		}
		//break ouf of while loop
		if(error || got_response) {
			break;
		}
	}

	if(got_response) {
		return 0;
	} if(error) {
		return -1;
	} else {
		return 1;
	}


}






void setup()
{

	// sets up the device to use the pins on the bottom left of the rPi header for serial communication.
	Serial = Uart(NRF_UART0, UARTE0_UART0_IRQn, 31, 7);
	Serial.begin(460800);
	Serial.println("Begin");


	// sets up the SPI connection to the DW3000 radio
	SPI = SPIClass(NRF_SPI2, SPI_MISO, SPI_CLK, SPI_MOSI);
	SPI.begin();


	//set up the backend components and feed them into the main DW3000 class
    dummy = new DummyStream();
    uart = new DWUart(*dummy);
	port = new DW3000Port(&SPI, SPI_CS, DW_RST, DW_IRQ);
	radio = new DW3000(uart, port);

	//hard reset
	port->reset();

	radio->dwt_softreset();

	while (!radio->dwt_checkidlerc()) // Need to make sure DW IC is in IDLE_RC before proceeding
	{
		Serial.println("Idle failed");
		delay(1000);
	}
	
	// uses DWT_LOADUCODE, which we don't have documentation for
	if (radio->dwt_initialise(0) == DWT_ERROR)
	{
		while (1)
		{
			Serial.println("Init failed");
			delay(1000);
		};
	}

	//enabling LEDs 2 and 3 (visible on the eval board) to blink on RX and TX
  	//radio->dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

	//manual LED control
	radio->gpio_init_output();


	while (radio->dwt_configure(&config))
	{
		Serial.println("Config failed");
		delay(1000);
	}
	
	//reset radio state: not doing this here results in a ~17s delay from the internal 40 bit sys timer
	//if we do a soft reset, we don't need to do this.
	//radio->dwt_writefastCMD(CMD_TXRXOFF);

	//only really needed if we're doing TX, it configures the phased locked loop, TX power, and delay settings
	//doing it for an rx-only radio shouldn't hurt.
	radio->dwt_configuretxrf(&txconfig_ch5);
	
	//just some quick-n-dirty pre-calculated values for 64 MHz PRF (pulled from an example file)
	//these should be calibrated when we make real code
	radio->dwt_setrxantennadelay(RX_ANT_DELAY);
	radio->dwt_settxantennadelay(TX_ANT_DELAY);


	//configure sleep settings
	radio->dwt_configuresleep(
		DWT_CONFIG //on wakeup, download the always-on register values to the "HIF"
		| DWT_PGFCAL, //re-calibrate PGF on wake

		DWT_PRES_SLEEP //preserve the SLEEP_EN bit, clears on wakeup
		| DWT_WAKE_CSN //wake up using the chip select line
		| DWT_WAKE_WUP //wake up using the wakeup pin
		| DWT_SLP_EN //enable sleep functionality
		//| DWT_SLEEP//adding this will use normal sleep mode instead of deep sleep mode
	);


	Serial.println("Ready");

}


#define INITIATOR

//initiator, does not do the final calculations
//loop_ds_init
void loop_ds_init() {


	//start with clean slate
	radio->clear_system_status();
	radio->dwt_writefastCMD(CMD_TXRXOFF);

	//make starting packet to send (doesn't have to be a TWR packet to start with)
	auto payload = "p0";
	UWBPacket packet = UWBPacket(
		get_uuid(), //this device's mac
		UWBPacket::BROADCAST_MAC,
		PacketType::Ok,
		(const uint8_t*)payload,
		sizeof(payload)
	);

	if(!send_packet(packet, DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED)) {
		//failed to send, restart loop
		Serial.println("Failed to send initial packet.");
		delay(1000);
		return;
	}

	//wait for RX
	auto rx_status = wait_for_message_with_timeout(200);
	if(rx_status) {
		Serial.print("RX Error: ");
		Serial.println(rx_status);
		return;
	}


	//parse the packet to get the timestamps from the other radio
	UWBPacket response_packet = get_packet();

	//timestamp when the initial packet was sent by us
	uint64_t initial_tx_timestamp = radio->get_tx_timestamp_u64();

	//timestamp when the response was got by us
	uint64_t rx_timestamp = radio->get_rx_timestamp_u64();

	//full round trip 
	uint64_t round_1_time = rx_timestamp - initial_tx_timestamp;
	

	//calculate and populate outgoing time (see above for a breakdown)
	uint32_t turnaround_time_us = 1000;
	uint64_t tx_timestamp = ((turnaround_time_us * UUS_TO_DWT_TIME + rx_timestamp) >> 8) & 0xFFFFFFFEUL;
	radio->dwt_setdelayedtrxtime((uint32_t)tx_timestamp);
	tx_timestamp = (tx_timestamp << 8) + TX_ANT_DELAY;
	uint64_t reply_2_time = tx_timestamp - rx_timestamp;

	//write packet
	RangingPacket range_p = RangingPacket(reply_2_time, round_1_time, 0);
	UWBPacket outgoing = UWBPacket(SOURCE_MAC, RESPONDER_MAC, PacketType::Ranging, range_p.get_compiled(), range_p.get_compiled_len());

	//send with delay
	if (!send_packet(outgoing, DWT_START_TX_DELAYED)) {
		Serial.println("Send result not successful");
		return;
	}

	Serial.println("Sent ranging response");


	//debug: flip LED
	flip = !flip;
	radio->gpio_set(2, flip);
	radio->gpio_set(3, !flip);

	delay(50);




}

//responder, makes the final calculations
//loop_ds_resp
void loop_ds_resp() {

	//clean slate
	radio->clear_system_status();
	radio->dwt_writefastCMD(CMD_TXRXOFF);

	//start listening
	radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

	//wait for initial message
	int result = wait_for_message_with_timeout(0, true);

	//failed to properly get packet
	if(result) {
		Serial.print("Error getting packet: ");
		Serial.println(result);
		return;
	}


	//we don't need to parse the packet, but if we did, that happens here.


	//the time we got the packet
	auto rx_timestamp = radio->get_rx_timestamp_u64();
	//calculate and populate outgoing time (see above for a breakdown)
	uint32_t turnaround_time_us = 1000;
	uint64_t tx_timestamp = ((turnaround_time_us * UUS_TO_DWT_TIME + rx_timestamp) >> 8) & 0xFFFFFFFEUL;
	radio->dwt_setdelayedtrxtime((uint32_t)tx_timestamp);
	tx_timestamp = (tx_timestamp << 8) + TX_ANT_DELAY;
	uint64_t reply_1_time = tx_timestamp - rx_timestamp;


	//write packet (content does not matter; the other radio will keep track of its own times)
	{
		auto payload = "abc";
		UWBPacket packet = UWBPacket(
			RESPONDER_MAC, //this device's mac
			SOURCE_MAC,
			PacketType::Ok,
			(const uint8_t*)payload,
			sizeof(payload)
		);

		if(!send_packet(packet, DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED)) {
			Serial.println("Send result not successful");
			return;
		}

	}

	//Serial.println("Got initial packet and sent response.");
	radio->clear_system_status();


	//wait for RX
	auto rx_status = wait_for_message_with_timeout(200);
	if(rx_status) {
		//Serial.print("RX Error: ");
		//Serial.println(rx_status);
		return;
	}


	//parse the packet to get the timestamps from the other radio
	UWBPacket response_packet = get_packet();


	switch(response_packet.get_packet_type()) {
		case PacketType::Ranging: {
			uint64_t round_2_time = radio->get_rx_timestamp_u64() - tx_timestamp;

			auto ranging_data = RangingPacket(response_packet.get_payload());

			uint64_t round_1_time = ranging_data.get_round_time();
			uint64_t reply_2_time = ranging_data.get_reply_time();

			//see page 249
			double top_val = ((double)round_1_time * (double)round_2_time) - ((double)reply_1_time * (double)reply_2_time);
			double bottom_val = ((double)round_1_time + (double)round_2_time + (double)reply_1_time + (double)reply_2_time);

			double time_of_flight = ((double)top_val)/((double)bottom_val);
			double distance = time_of_flight * SPEED_OF_LIGHT * DWT_TIME_UNITS;

			// Serial.print("Rounds: ");
			// print_u64(round_1_time);
			// Serial.print(" ");
			// print_u64(round_2_time);
			// Serial.print(" Replies: ");
			// print_u64(reply_1_time);
			// Serial.print(" ");
			// print_u64(reply_2_time);
			// Serial.print(" ");

			//Serial.print("Distance: ");		
			Serial.println(distance);

			break;
		}
		default: {
			return;

			Serial.print("Wrong RX Packet type. Expected Ranging packet ");
			Serial.println(response_packet.get_packet_type());

			if(response_packet.get_packet_type() == 0) {
				//debug: flip LED
				flip = !flip;
				radio->gpio_set(2, flip);
				radio->gpio_set(3, !flip);
			}

			return;
		}
	}


}



void loop(){
#ifdef INITIATOR
	loop_ds_init();
#else
	loop_ds_resp();
#endif	
}
