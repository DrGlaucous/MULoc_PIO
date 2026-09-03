#include <vector>
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


DWUart* uart = nullptr;
DW3000Port* port = nullptr;
DW3000* radio = nullptr;

//how the radio should be configured for the session.
static const dwt_config_t config = {
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
bool flop = false;

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
//if switch_to_rx is true, the radio will immediately go into RX mode after transmission (a reset fast command needs to be sent to get out of this mode)
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
	//for(int i = 0; i < 50; ++i) {
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


//a universal collection of timing values
//to be stored in a vector and eventually transmitted out to all anchors that sent a response initially
//also held by the anchor for each tag it needs to reply to
typedef struct ResponderHolder {
	uint64_t mac; //mac address of the device we got the packet from
	uint64_t rx_timestamp_radio; //radio timestamp when we got the packet

	//these are only used by the anchor
	uint64_t tx_timestamp_radio; //radio timestamp when we will send the packet out in the future (used by the anchor only, see tx_timestamp_mcu_micros)
	uint64_t rx_timestamp_mcu_micros; //microcontroller timestamp when it got the packet (used by the anchor only)
	uint64_t tx_timestamp_mcu_micros; //microcontroller timestamp when it should send the packet (used by the anchor only)
	bool sent_reply; //true if we sent the packet back at the time: tx_timestamp_mcu_micros
} ResponderHolder;


////////////////////main methods

void setup()
{


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

//initiator, does not do the final calculations (I.E. tag)
//loop_ds_init
void loop() {


	//start with clean slate
	radio->clear_system_status();
	radio->dwt_writefastCMD(CMD_TXRXOFF);

	radio->gpio_set(2, true);

	//send a frame 0 to everyone
	{
		//make starting packet to send (doesn't have to be a TWR packet to start with)
		auto payload = RangingPacket(0, 0, RangingFrameNum::Request);
		UWBPacket packet = UWBPacket(
			get_uuid(), //this device's mac
			UWBPacket::BROADCAST_MAC, //send to everyone
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
	}

	uint64_t starting_time = micros();


	std::vector<ResponderHolder> responses = {};

	//allow and respond to new packets for X seconds before cutting them off and sending out another frame 0
	uint32_t reception_start = millis();
	while(millis() - reception_start < 100) {

		auto response = radio->check_for_rx();
		

		if(response == 1) {
			//got a good packet, log it

			//parse the packet to get the timestamps from the other radio
			UWBPacket response_packet = get_packet();

			//if it's destined for us (ignoring broadcast addresses, since those will only come from tags)
			if(response_packet.get_dest_uuid() == get_uuid()) {

				//checking that the packet is a ranging packet and that it's a frame 1 is not strictly needed, but if we want to do it, we do it here.
				//...

				//add to our list of radios to reply to
				ResponderHolder aa = {};
				aa.mac = response_packet.get_source_uuid();
				aa.rx_timestamp_radio = radio->get_rx_timestamp_u64();
				responses.push_back(aa);

			}		

		}
		
		//go back to listening
		if(response) {
			//even if it's not successful, it will still put the radio in idle mode, so re-enable listening mode is needed
			radio->clear_system_status();
			radio->dwt_writefastCMD(CMD_RX);
		}


	}



	//should now have a list of responses we need to give a final reply to
	size_t responses_size = responses.size();
	if(responses.size() > 0) {

		//prep radio to transmit
		radio->clear_system_status();
		radio->dwt_writefastCMD(CMD_TXRXOFF);

		//time when we sent it initially
		uint64_t first_tx_time = radio->get_tx_timestamp_u64();


		//the time it took from sending out the first packet to sending out the next one
		uint64_t waited_time = micros() - starting_time;
		//plus some extra because formatting the outgoing packet will also take some extra time
		uint64_t extra_turnaround_time_us = 5000;
		uint64_t turnaround_time = waited_time + extra_turnaround_time_us;

		uint64_t curr_time = radio->get_sys_timestamp_u64();


		//calculate everything in radio units, this is the future time when we send it out
		uint64_t tx_timestamp = ((turnaround_time * UUS_TO_DWT_TIME + first_tx_time) >> 8) & 0xFFFFFFFEUL;
		radio->dwt_setdelayedtrxtime((uint32_t)tx_timestamp);
		tx_timestamp = (tx_timestamp << 8) + TX_ANT_DELAY;

		
		MultiRangingPacket c_ranging = MultiRangingPacket();
		//add all items in the list to the MultiRangingPacket
		for(size_t i = 0; i < responses_size; ++i) {

			auto single_resp = responses[i];

			uint64_t round_time = single_resp.rx_timestamp_radio - first_tx_time;
			uint64_t reply_time = tx_timestamp - single_resp.rx_timestamp_radio;

			RangingPacket replyP = RangingPacket(reply_time, round_time, RangingFrameNum::Final);

			//push back, check for out-of room (if we are out of room, then it doesn't matter, we'll just skip the rest since we should have enough anchors to get a good datapoint)
			if(!c_ranging.add_packet(replyP, single_resp.mac)) {
				break;
			}

		}
			

		//MultiRangingPacket is now formatted, append to a UWBPacket
		UWBPacket outgoing = UWBPacket(get_uuid(), UWBPacket::BROADCAST_MAC, PacketType::MultiRanging, c_ranging.get_compiled(), c_ranging.get_compiled_len());
		

		//UWBPacket test = UWBPacket();


		//send with delay
		if (!send_packet(outgoing, DWT_START_TX_DELAYED)) {
			uint64_t tock = micros() - (waited_time + starting_time);
			uint64_t curr_time2 = radio->get_sys_timestamp_u64();
			//Serial.println("Send result not successful");
		} else {
			//Serial.println("Sent ranging response");
			
			uint64_t tock = micros() - (waited_time + starting_time);
			//debug: flip LED
			flip = !flip;		
			radio->gpio_set(3, !flip);
		}
		

	}


	radio->gpio_set(2, false);

	//wait for next time, with random spacing to avoid synching to another tag
	delay(500 + random(100));




}



//responder, makes the final calculations (I.E. anchor)
//loop_ds_resp
void loop_ds_resp() {

	//clean slate	
	radio->clear_system_status();
	radio->dwt_writefastCMD(CMD_TXRXOFF);

	//start listening
	radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);


	std::vector<ResponderHolder> responses = {};
	auto last_millis = millis();

	//wait for a broadcast frame0 message
	while(1) {


		auto response = radio->check_for_rx();

		//got something
		if(response == 1) {

			//ensure radio is in IDLE state (it should be, just making sure)
			radio->dwt_writefastCMD(CMD_TXRXOFF);
			radio->clear_system_status();
			
			//parse the packet to get the timestamps from the other radio
			UWBPacket response_packet = get_packet();

			//if it's destined for us (mainly broadcast messages; the tags only send those out)
			auto uuid = response_packet.get_dest_uuid();
			if(uuid == UWBPacket::BROADCAST_MAC || uuid == get_uuid()) {

				switch(response_packet.get_packet_type()) {

					//initial response
					case PacketType::Ranging: {

						flip = !flip;
						radio->gpio_set(2, flip);

						ResponderHolder aa;// = ResponderHolder {
						aa.mac = response_packet.get_source_uuid(); //source mac
						aa.rx_timestamp_radio = radio->get_rx_timestamp_u64(); //radio arrival time
						aa.tx_timestamp_radio = 0; //radio departure time (will be set later)
						aa.rx_timestamp_mcu_micros = micros(); //time on the MCU when we got the packet in, used for turnaround time when we send this response back,
						aa.tx_timestamp_mcu_micros = aa.rx_timestamp_mcu_micros + random(10000); //time when we will send the packet out, shuffled to avoid collisions
						aa.sent_reply = false;

						//check if packet from this MAC address already exists, then override it if it does
						bool found_existing = false;
						for(size_t i = 0; i < responses.size(); ++i) {
							if(responses[i].mac == aa.mac) {
								//copy over to existing entry
								responses[i] = aa;
								found_existing = true;
								break;
							}
						} 
						//otherwise add it
						if(!found_existing) {
							responses.push_back(aa);
						}



						break;
					}
					//final response
					case PacketType::MultiRanging: {

						flop = !flop;
						radio->gpio_set(3, flop);


						//check if our MAC is in there
						MultiRangingPacket multi = MultiRangingPacket(response_packet.get_payload(), response_packet.get_payload_len());


						//check all packets in the ranging packet to see if one was addressed to us
						for(int i = 0; i < multi.get_packet_count(); ++i) {

							//is addressed to us
							auto mac = multi.get_mac_at(i);
							if(mac == get_uuid()) {

								//find the ResponseHandler that corresponds to this mac (if it exists. It should)
								size_t j = 0;
								size_t resp_size = responses.size();
								bool found = false;
								for(;j < resp_size; ++j) {
									if(responses[j].sent_reply == true && responses[j].mac == response_packet.get_source_uuid()) {


										RangingPacket a = multi.get_packet_at(i);
										//note: we have an issue where we occasionally get something like FFFFFF077204D1FD from get_reply_time() (instead of the intended 077204D1FD...)										
										//we also get these from the other calculations. The reason is because of byte overflow issues. Masking out the upper 3 bytes solves this problem
										auto reply_2_time = a.get_reply_time();
										auto round_1_time = a.get_round_time();

										uint64_t reply_1_time = (responses[j].tx_timestamp_radio - responses[j].rx_timestamp_radio) & 0xFFFFFFFFFF;
										uint64_t round_2_time = (radio->get_rx_timestamp_u64() - responses[j].tx_timestamp_radio) & 0xFFFFFFFFFF;

										// if((int64_t)round_2_time < -1) {
										// 	uint64_t rr = radio->get_rx_timestamp_u64();
										// 	uint32_t apple = 3;
										// 	apple += 1;
										// }

										//see page 249
										double top_val = ((double)round_1_time * (double)round_2_time) - ((double)reply_1_time * (double)reply_2_time);
										double bottom_val = ((double)round_1_time + (double)round_2_time + (double)reply_1_time + (double)reply_2_time);

										double time_of_flight = ((double)top_val)/((double)bottom_val);
										double distance = time_of_flight * SPEED_OF_LIGHT * DWT_TIME_UNITS;
										
										if(0) {
											Serial.print("Cycle 1: ");
											print_u64(round_1_time, HEX);
											Serial.print(" ");
											print_u64(reply_1_time, HEX);
											Serial.print(" Cycle 2: ");
											print_u64(round_2_time, HEX);
											Serial.print(" ");
											print_u64(reply_2_time, HEX);
											Serial.print(" Diffs: ");
											print_u64(round_1_time - reply_1_time, HEX);
											Serial.print(" ");
											print_u64(round_2_time - reply_2_time, HEX);

											Serial.print(" Round 2 values: ");
											print_u64(responses[j].tx_timestamp_radio, HEX);
											Serial.print(" ");
											print_u64(radio->get_rx_timestamp_u64(), HEX);

											Serial.print(" ");
											Serial.print(resp_size);
										}

										Serial.print("MAC: ");
										print_u64(response_packet.get_source_uuid(), HEX);
										Serial.print(" Distance: ");
										Serial.println(distance);



										//found the cached response, no need to check anymore
										found = true;
										break;
									}									
								}

								//erase the entry that we found the distance of
								if(found) {
									responses.erase(responses.begin() + j);
								}


								//found the packet, no need to check any more
								break;

							}

						}

						//Serial.print("Multi-Packet count: ");
						//Serial.println(multi.get_packet_count());

						break;
					}
					default: {
						Serial.print("Other packet type");
						break;
					}
				}


			}




		}
		

		//go back to listening
		if(response) {
			//even if it's not successful, it will still put the radio in idle mode, so re-enable listening mode is needed
			radio->clear_system_status();
			radio->dwt_writefastCMD(CMD_RX);

		}


		//check to send a packet
		if(last_millis != millis()) {
			last_millis = millis();

			auto micros_now = micros();

			//iterate through all the queued packets to see if there are any ready to go out
			//int prune_idx = -1;
			std::vector<size_t> prune_idx = {};

			for(size_t i = 0; i < responses.size(); ++i) {

				//waited long enough, it's time to send a packet (note: this is not overflow safe!)
				if(responses[i].tx_timestamp_mcu_micros <= micros_now && responses[i].sent_reply == false) {


					//prep radio to transmit
					radio->clear_system_status();
					radio->dwt_writefastCMD(CMD_TXRXOFF);


					ResponderHolder& rhold = responses[i];

					auto waited_time = micros_now - rhold.rx_timestamp_mcu_micros;
					uint64_t extra_turnaround_time_us = 4000;
					uint64_t turnaround_time = waited_time + extra_turnaround_time_us;

					//calculate everything in radio units, this is the future time when we send it out
					uint64_t tx_timestamp = ((turnaround_time * UUS_TO_DWT_TIME + rhold.rx_timestamp_radio) >> 8) & 0xFFFFFFFEUL;
					radio->dwt_setdelayedtrxtime((uint32_t)tx_timestamp);
					tx_timestamp = (tx_timestamp << 8) + TX_ANT_DELAY;

					//send back to anchor
					RangingPacket response_p = RangingPacket(0, 0, RangingFrameNum::Response);
					UWBPacket outgoing = UWBPacket(get_uuid(), rhold.mac, PacketType::Ranging, response_p.get_compiled(), response_p.get_compiled_len());
					
					rhold.tx_timestamp_radio = tx_timestamp;
					rhold.sent_reply = true;

					if (!send_packet(outgoing, DWT_START_TX_DELAYED)) {
						//Serial.println("Send result not successful");
						//return;
					}

					radio->clear_system_status();

				}


				//packets older than 5 seconds get pruned
				if(responses[i].tx_timestamp_mcu_micros <= micros_now - 5000000) {
					prune_idx.push_back(i);
				}

			}
			
			//remove the marked items
			auto prunables = prune_idx.size();
			if(prunables > 0) {
				for(size_t i = 0; i < prunables; ++i) {
					responses.erase(responses.begin() + prune_idx[i]);
				}
			}

			//go back to listening again
			radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

		}



	}



}

