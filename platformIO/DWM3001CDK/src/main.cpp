#include <vector>
#include <Arduino.h>
#include "constants.h"
#include "dw3000.h"
#include "dw3000_regs.h"
#include "dw3000_shared_defines.h"
#include "SPI.h"
#include "packet.h"



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


//blocks until the radio gets a message in or until timeout_ms is reached.
//we can also wait for timeout using dwt_setrxtimeout...
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



////////////////////main methods

//the mainloop specific to the anchors
void loop_anchor() {

    //additional anchor setup


	//this packet is saved through multiple RX and TX operations, updated whenever a new packet from another anchor is gotten
	TokenRingPacket persistent_tr_packet = TokenRingPacket();
	persistent_tr_packet.set_index(ANCHOR_ID);


    AnchorState anchor_state = AnchorState::Listening;
    int error_count = 0;

    //clean slate (possibly redundant)
	radio->clear_system_status();
	radio->dwt_writefastCMD(CMD_TXRXOFF);


    //we're the starting anchor, initiate message chain and then wait for a response
    if(ANCHOR_ID == 0) {

        
        anchor_state = AnchorState::Sending;
        
        radio->dwt_setrxtimeout(RX_TIMEOUT);

        //construct packet with basic info
        TokenRingPacket outgoing_tr = TokenRingPacket();
        outgoing_tr.set_index(ANCHOR_ID);
        outgoing_tr.set_sequence_no(0);

        //wrap it in a main packet
        UWBPacket outgoing_main = UWBPacket(
            get_uuid(),
            UWBPacket::BROADCAST_MAC,
            PacketType::TokenRing,
            outgoing_tr.get_compiled(),
            outgoing_tr.get_compiled_len()
        );

		//send message and immediately wait for response
		send_packet(outgoing_main, DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);



    }


    //the "true" mainloop
    while(1) {

        //put radio into RX mode
        //(counterintuitively, the anchor in the "Sending" state is already in RX mode by this point)
        //the condition here is just to make the other anchors listen for that initial packet
        if(anchor_state == AnchorState::Listening) {
            //we might not need to set this every frame... oh, well. We'll leave it for now.
            radio->dwt_setrxtimeout(RX_TIMEOUT);
            
            //start listening
	        radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);
        } else {

            //mark the transmitter as listening as well
            anchor_state = AnchorState::Listening;
        }


		//if we send the message, we will block here until the next anchor sends a message
        if(wait_for_message_with_timeout(0, true)) {
            //successful RX

			//reset radio status
			radio->clear_system_status();

			//reset error count
            error_count = 0;

            //get incoming frame
            auto frame = get_packet();

			//ensure correct packet type (we assume it will always be a tokenRing packet)
			if(frame.get_packet_type() != PacketType::TokenRing) {
				continue;
			}

			//get token ring from other sender
			TokenRingPacket tr_packet = TokenRingPacket(frame.get_payload());

			//get ID of sender
			uint8_t sender_id = tr_packet.get_index();
			uint8_t sequence_no = tr_packet.get_sequence_no();

			//get time of arrival
			uint64_t rx_time = radio->get_rx_timestamp_u64();

			//update the packet cache with it
			AnchorInfoPacket latest_info = persistent_tr_packet.get_packet_at(sender_id); //get
			latest_info.set_rx_time(rx_time); //update
			persistent_tr_packet.set_packet_at(latest_info, sender_id); //set

			//check and transmit the next message
			if(
				(ANCHOR_ID == (sender_id + 1) % ANCHOR_NUM) //next sender is us
				&& !((ANCHOR_ID == 0) && (sequence_no % 2 == 1)) //we aren't anchor 0 during a frequency hop
			) {
				auto turnaround_time = TURNAROUND_TIME_US;

				//completed the loop, increment the sequence number
				if(ANCHOR_ID == 0) {
					sequence_no += 1;

					//optional: the original code had this, but it's the same value as the normal turnaround time
					turnaround_time = TURNAROUND_HOP_TIME_US;
				}

				//set the outgoing time and give it to the radio
				uint64_t tx_timestamp = ((turnaround_time * UUS_TO_DWT_TIME + rx_time) >> 8) & 0xFFFFFFFEUL;
				radio->dwt_setdelayedtrxtime((uint32_t)tx_timestamp);
				//tx_timestamp = (tx_timestamp << 8) + TX_ANT_DELAY; //only needed if we want to put the tx timestamp in the outgoing packet. We don't really need to do that with this implementation since the turnaround time is fixed

				//package packet up for transmission
				persistent_tr_packet.set_sequence_no(sequence_no);
				UWBPacket new_outgoing = UWBPacket(
					get_uuid(),
					UWBPacket::BROADCAST_MAC,
					PacketType::TokenRing,
					persistent_tr_packet.get_compiled(),
					persistent_tr_packet.get_compiled_len());

				//set timeout for reception
				radio->dwt_setrxtimeout(RX_TIMEOUT);
				radio->dwt_setrxaftertxdelay(RX_AFTER_TX_DELAY);

				//send packet and change state (optionally expecting a response or not)
				if(
					(ANCHOR_ID == ANCHOR_NUM - 1) //we are the last anchor
					&& (sequence_no % 2 == 1) //the sequence number is odd
				) {
					//don't expect a response; we will be changing frequencies
					send_packet(new_outgoing, DWT_START_TX_DELAYED);
				} else {
					send_packet(new_outgoing, DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
					anchor_state == AnchorState::Sending;
				}
				
			}


			//collect novel ranging data (todo: find DW3000 equivalents to the data from the DW1000)


			//get phase of arrival, see page 180. This record is 14 bits long (in the DW1000, it is 7 bits long)
			uint16_t phase_cal = 0;
			radio->dwt_readfromdevice(IP_TOA_HI_ID, 1, 2, (uint8_t*)&phase_cal);

			//lots of the values we need are now stored inside this struct
			//dwt_rxdiag_t diagnostics = {};
			//radio->dwt_readdiagnostics(&diagnostics);
			//uint16_t fp_index = diagnostics.ipatovPeak;
			//radio->dwt_readaccdata()




        } else {
            //unsuccessful RX


        }
    }

}


void loop_tag() {



}

//starts SPI, Serial, and the DW3000
void setup() {
	// sets up the device to use the pins on the bottom left of the rPi header for serial communication.
	Serial = Uart(NRF_UART0, UARTE0_UART0_IRQn, 31, 7);
	Serial.begin(115200);
	Serial.println("Begin");


	// sets up the SPI connection to the DW3000 radio
	SPI = SPIClass(NRF_SPI2, SPI_MISO, SPI_CLK, SPI_MOSI);
	SPI.begin();

    //set up the backend components and feed them into the main DW3000 class
	uart = new DWUart(115200);
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

    //set up general radio configuration
	while (radio->dwt_configure(&config_ch5))
	{
		Serial.println("Config failed");
		delay(1000);
	}

    //set up radio transmission configuration
    radio->dwt_configuretxrf(&txconfig_ch5);

    radio->dwt_setrxantennadelay(RX_ANT_DELAY);
	radio->dwt_settxantennadelay(TX_ANT_DELAY);

    //in my other code, I put sleep settings here. I don't really need that for this example, so I exclude it.

    //we should also probably set the pan_id here, but I'll skip that for now.
    //radio->dwt_setpanid(1);
    
    Serial.println("Ready");
}


void loop() {
    delay(1000);
}















