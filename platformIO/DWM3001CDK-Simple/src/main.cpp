#include <vector>
#include <Arduino.h>
#include "constants.h"
#include "dw3000.h"
#include "dw3000_regs.h"
#include "dw3000_shared_defines.h"
//#include "dw3000_device_api.h"
#include "SPI.h"
#include "packet.h"


DummyStream* dummy = nullptr;
DWUart* uart = nullptr;
DW3000Port* port = nullptr;
DW3000* radio = nullptr;



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
void print_u64(uint64_t value, int base) {
	Serial.print((uint32_t)(value >> 32), base);
	Serial.print((uint32_t)(value & 0xFFFFFFFF), base);
}

//writes a UWBPacket out to the radio and waits for it to respond with the send status
//see: dwt_starttx to understand what modes are valid
bool send_packet(UWBPacket& packet, uint8_t mode) {
	
	//write actual data to the outgoing buffer, automatically accounts for >127 packet sizes
	radio->dwt_writetxdata(packet.get_compiled_len(), packet.get_compiled(), 0);

	//append FC data (2 byte checksum), identify this packet as a ranging packet
	radio->dwt_writetxfctrl(packet.get_compiled_len() + FCS_LEN, 0, 1);

	//start TX mode
	int first_send_error = radio->dwt_starttx(mode);

	//happens if the delayed time has passed, it puts the radio into off mode
	if(first_send_error != DWT_SUCCESS) {
		//Serial.print("Delay send error: ");
		//Serial.println(first_send_error);
		return false;
	}

	//check for a successful transmit with timeout
	bool send_error = true;
	
    //tx visual debug: blink LED while waiting for the frame to go out
	pinMode(14, OUTPUT);
	digitalWrite(14, false);

    while(1) {
		if(radio->check_frame_tx_success()) {
			send_error = false;
			break;
		}
	}

    digitalWrite(14, true);

	//had problem sending packet, reset radio status and return
	if(send_error) {
		radio->clear_system_status();
		radio->dwt_writefastCMD(CMD_TXRXOFF);
		//Serial.println("Send Error");
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


//check for gotten frame, returns 0 on nothing, 1 on success, 2 on bad checksum, 3 on error
int clone_check_for_rx() {

    //get current status
    int sys_stat = radio->dwt_read32bitreg(SYS_STATUS_ID);
    
    //got packet
    if((sys_stat & SYS_STATUS_RXFCG_BIT_MASK) > 0) {
        return 1;
    }
    //got packet (bad checksum, if we got a good checksum, both this bit and the one above is set. Otherwise, only this bit will be set.)
    else if ((sys_stat & SYS_STATUS_RXFR_BIT_MASK) > 0) {
        return 2;
    } 
    //got error
    else if ((sys_stat & SYS_STATUS_ALL_RX_ERR) > 0) {
        return 3;
    }
    return 0;

    //in checking for RX errors, the simple library used:
    //(1 << 26)     RXSTO yes
    //(1 << 21)     RXPTO no (preamble detection timeout)
    //(1 << 18)     CIAERR yes
    //(1 << 17)     RXFTO no (Receive Frame Wait Timeout)
    //(1 << 16)     RXFSL yes
    //(1 << 15)     RXFCE yes     (rxfcg is not included here)
    //(1 << 12)     RXPHE yes
    //#define SYS_STATUS_RX_ERR 0x4279000

    //the rx example for the makerfabs library used:
    //SYS_STATUS_RXFCG_BIT_MASK no
    //SYS_STATUS_RXPHE_BIT_MASK yes
    //SYS_STATUS_RXFCE_BIT_MASK yes
    //SYS_STATUS_RXFSL_BIT_MASK yes
    //SYS_STATUS_RXSTO_BIT_MASK yes
    //SYS_STATUS_ARFE_BIT_MASK no (automatic frame filtering rejection)
    //SYS_STATUS_CIAERR_BIT_MASK yes

}

//blocks until the radio gets a message in or until timeout_ms is reached.
//we can also wait for timeout using dwt_setrxtimeout...
//the radio must already be set to the correct mode with a fast command!
//returns 0 on timeout, 1 on success, 2 on bad checksum, 3 on general error
int wait_for_message_with_timeout(uint32_t timeout_ms, bool no_timeout = false) {


	auto tx_time = millis();
	while(no_timeout || (millis() - tx_time < timeout_ms)) {
		auto response = radio->check_for_rx();
        if(response) {
            return response;
        }

	}

	return 0;


}



//set the radio's output channel
void set_channel_config(bool is_freq_5) {
    radio->dwt_forcetrxoff();
    if(is_freq_5) {
        //Serial.print("CHN: 5 ");
        //Serial.print(" ");

        while(radio->dwt_configure(&config_ch5) != DWT_SUCCESS);
        radio->dwt_configuretxrf(&txconfig_ch5); 
    } else {
        //Serial.print("CHN: 9 ");
        //Serial.print(" ");

        while(radio->dwt_configure(&config_ch9) != DWT_SUCCESS);
        radio->dwt_configuretxrf(&txconfig_ch9);
    }
    //force it to log important telemetry
    radio->dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);
}

//set the time at which the next transaction should happen
//takes the time in microseconds until the next transaction, returns it in radio units
uint64_t set_outgoing_time(uint64_t turnaround_time_micros, uint64_t rx_time_radio) {
    
    //convert micros to DWT tome and add it to the radio time, then shift it into the delay time register since that one chops off the LSByte
    uint64_t tx_timestamp = ((turnaround_time_micros * UUS_TO_DWT_TIME + rx_time_radio) >> 8) & 0xFFFFFFFEUL;
    radio->dwt_setdelayedtrxtime((uint32_t)tx_timestamp);

    //make units the same as radio->get_rx_timestamp_u64
    return (tx_timestamp << 8) + TX_ANT_DELAY;
}

////////////////////main methods



bool flip = false;
//initiator, does not do the final calculations
//loop_ds_init
void loop_ds_tag() {


	//start with clean slate
	radio->clear_system_status();
	radio->dwt_writefastCMD(CMD_TXRXOFF);

	//make starting packet to send (doesn't have to be a TWR packet to start with)
	auto payload = RangingPacket(0, 0, RangingFrameNum::Request);
	UWBPacket packet = UWBPacket(
		get_uuid(), //this device's mac
		UWBPacket::BROADCAST_MAC,
		PacketType::Ranging,
		payload.get_compiled(),
		payload.get_compiled_len()
	);

	if(!send_packet(packet, DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED)) {
		//failed to send, restart loop
		Serial.println("Failed to send initial packet.");
		delay(1000);
		return;
	}

	//wait for RX
	auto rx_status = wait_for_message_with_timeout(1000);
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
	RangingPacket range_p = RangingPacket(reply_2_time, round_1_time, RangingFrameNum::Final);
	UWBPacket outgoing = UWBPacket(get_uuid(), UWBPacket::BROADCAST_MAC, PacketType::Ranging, range_p.get_compiled(), range_p.get_compiled_len());

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

	delay(200);




}

//responder, makes the final calculations
//loop_ds_resp
void loop_ds_anchor() {

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
			get_uuid(), //this device's mac
			UWBPacket::BROADCAST_MAC,
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
	auto rx_status = wait_for_message_with_timeout(1000);
	if(rx_status) {
		Serial.print("RX Error: ");
		Serial.println(rx_status);
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

			Serial.print("Distance: ");		
			Serial.println(distance);

			break;
		}
		default: {
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






//starts SPI, Serial, and the DW3000
void setup() {
	// sets up the device to use the pins on the bottom left of the rPi header for serial communication.
	Serial = Uart(NRF_UART0, UARTE0_UART0_IRQn, 31, 7);
	Serial.begin(BAUD_RATE);
	Serial.println("Begin");




	// sets up the SPI connection to the DW3000 radio
	SPI = SPIClass(NRF_SPI2, SPI_MISO, SPI_CLK, SPI_MOSI);
	SPI.begin();

    //set up the backend components and feed them into the main DW3000 class
	//uart = new DWUart(BAUD_RATE);
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
  	radio->dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    //manual LED control
	//radio->gpio_init_output();

    //happens in the loop, don't need to do it here
    //set up general radio configuration
	// while (radio->dwt_configure(&config_ch5))
	// {
	// 	Serial.println("Config failed");
	// 	delay(1000);
	// }
    // //set up radio transmission configuration
    // radio->dwt_configuretxrf(&txconfig_ch5);

    radio->dwt_setrxantennadelay(RX_ANT_DELAY);
	radio->dwt_settxantennadelay(TX_ANT_DELAY);

    //in my other code, I put sleep settings here. I don't really need that for this example, so I exclude it.

    //we should also probably set the pan_id here, but I'll skip that for now.
    //radio->dwt_setpanid(1);
    
    Serial.println("Ready");
}


void loop() {


#ifdef TAG
    loop_ds_tag();
#else
    loop_ds_anchor();
#endif

}















