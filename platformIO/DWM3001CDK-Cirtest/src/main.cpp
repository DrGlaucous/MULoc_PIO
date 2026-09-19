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
        //got timeout
        else if ((sys_stat & SYS_STATUS_ALL_RX_TO) > 0) {
            return 4;
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


	bool got_response = false;
	bool error = false;
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

//converts an unsigned 24 bit integer into a signed one, mainly used by CIR stuff
int32_t convert_u24_to_i24(uint32_t raw_24bit) {
    // Mask to ensure we only have 24 bits of data
    raw_24bit &= 0x00FFFFFF; 

    //if the 24th bit (0x800000) is set, it's negative
    if (raw_24bit & 0x00800000) {
        return static_cast<int32_t>(raw_24bit | 0xFF000000); //sign extend
    }
    
    return static_cast<int32_t>(raw_24bit);
}

//prints the CIR packet array to the terminal, format: [phase0],[magnitude0],[phase1],[magnitude1],[phase2]...
void print_cir_packet(const CirDebugPacket& packet) {
    for(int i = 0; i < CirDebugPacket::VALUE_COUNT; ++i) {
        uint32_t real = 0;
        uint32_t img = 0;
        packet.get_complex_at(i, &real, &img);

        float real_f = convert_u24_to_i24(real);
        float img_f = convert_u24_to_i24(img);

        float phase = atan2(img_f, real_f);
		float amplitude = sqrtf(img_f * img_f + real_f * real_f);


        Serial.print(phase);
        Serial.print(",");
        Serial.print(amplitude);
        Serial.print(",");
    }
}

////////////////////main methods

//just blasts packets. How fast can we go?
bool freq_flipper = false;
void loop_test() {

    auto tick = micros();

    auto outgoing = "Hello";
    //wrap outgoing_tr_packet in a main packet
    UWBPacket outgoing_main = UWBPacket(
        get_uuid(),
        UWBPacket::BROADCAST_MAC,
        PacketType::UserDefined,
        (uint8_t*)outgoing,
        sizeof(outgoing)
    );

    auto midpoint = micros();

    //send message and immediately start waiting for response (non-blocking)
    send_packet(outgoing_main, DWT_START_TX_IMMEDIATE);

    auto first_half = midpoint - tick;
    auto total = micros() - tick;
    Serial.print(first_half);
    Serial.print(" ");
    Serial.println(total);

    set_channel_config(freq_flipper);
    freq_flipper = !freq_flipper;
    //delay(1000);


}



//custom anchor loop
void loop_a_custom() {

    //we'll just start with a simple token ring loop between ANCHOR_NUM anchors

    //start with clean slate
    radio->clear_system_status();

    //ensure frequency is set to 5 to begin with
    bool is_freq_5 = true;
    set_channel_config(is_freq_5);

    //as we get info from the other anchors, it goes here
    TokenRingPacket updating_packet = TokenRingPacket();
    updating_packet.set_index(ANCHOR_ID);

    //after a full round, the data from updating_packet gets copied into here to send out to everyone else the next chance we get
    TokenRingPacket outgoing_packet = TokenRingPacket();
    outgoing_packet.set_index(ANCHOR_ID);


    //initiator
    if(ANCHOR_ID == 0) {

        UWBPacket outgoing = UWBPacket(
            get_uuid(),
            UWBPacket::BROADCAST_MAC,
            PacketType::TokenRing,
            outgoing_packet.get_compiled(),
            outgoing_packet.get_compiled_len()
        );
        send_packet(outgoing, DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
    } else {

        //start listening
	    radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }

    //runs as long as we don't have packet timeouts or errors
    while(1) {

        auto message_result = wait_for_message_with_timeout(RX_TIMEOUT_MS);

        //got message
        if(message_result == 1) {

            //uint32_t tick = micros();

            //reset radio status
			radio->clear_system_status();

            //get frame data inside "updating_packet"
            auto frame = get_packet();
            auto rx_payload = TokenRingPacket(frame.get_payload());
            uint8_t anchor_number = rx_payload.get_index();
            uint8_t sequence_number = rx_payload.get_sequence_no();

			//get time of arrival
			uint64_t rx_time = radio->get_rx_timestamp_u64();

            //store important data
            {
                //read it first


			    //lots of the values we need are now stored inside this struct
			    dwt_rxdiag_t diagnostics = {};
			    radio->dwt_readdiagnostics(&diagnostics);

                //1:1 with the DW1000 version
                //raw register: IP_DIAG_8.IP_FP
                uint16_t fp_index = diagnostics.ipatovFpIndex >> 6; //bit shifting removes the fractional part

                //higher resolution than the DW1000 version
                //raw register: IP_DIAG_12.IP_NACC
                uint16_t rx_pc = diagnostics.ipatovAccumCount;
                
                //also read from: IP_DIAG_1, the value is 17 bits long
                uint32_t max_gc = diagnostics.ipatovPower;


                //get phase of arrival, see page 180. This record is 14 bits long (in the DW1000, it is 7 bits long)
                //it is a signed two's compliment integer, I need to sign-extend it and convert it to radians (divide by 2^11)
                //uint16_t phase_cal = 0;
                //radio->dwt_readfromdevice(IP_TOA_HI_ID, 1, 2, (uint8_t*)&phase_cal);
                uint16_t phase_cal = diagnostics.ipatovPOA; //this is the exact same thing

                uint8_t complex_byte_len = 6;
                //extra 1 for the dummy leading byte we get when starting the read
                uint8_t cir_buffer[complex_byte_len * CIR_LEN + 1] = {};
                radio->dwt_readaccdata(cir_buffer, (complex_byte_len * CIR_LEN + 1), fp_index * complex_byte_len);

                //put the data we collected into our persistent payload

                //the original code took the second entry of three in the CIR buffer
                uint32_t cir_real = cir_buffer[1 + complex_byte_len] //lo
                    | cir_buffer[1 + complex_byte_len + 1] << 8 //mid
                    | cir_buffer[1 + complex_byte_len + 2] << 16; //hi

                uint32_t cir_img = cir_buffer[1 + complex_byte_len * 2] //lo
                    | cir_buffer[1 + complex_byte_len * 2 + 1] << 8 //mid
                    | cir_buffer[1 + complex_byte_len * 2 + 2] << 16; //hi


                //stash diagnostics
                AnchorInfoPacket rx_anchor_info = AnchorInfoPacket();
                rx_anchor_info.set_cir_real(cir_real);
                rx_anchor_info.set_cir_imaginary(cir_img);
                rx_anchor_info.set_phase_correction(phase_cal);
                rx_anchor_info.set_preamble_accumulation(rx_pc);
                rx_anchor_info.set_max_growth_cir(max_gc);
                rx_anchor_info.set_rx_time(rx_time);

                //test diagnostics data to ensure AnchorInfoPacket is working correctly
                // rx_anchor_info.set_cir_real(0xFF114433);
                // rx_anchor_info.set_cir_imaginary(0x7A887766);
                // rx_anchor_info.set_phase_correction(0x5566);
                // rx_anchor_info.set_preamble_accumulation(0x7744);
                // rx_anchor_info.set_max_growth_cir(0xAAEEFF44);
                // rx_anchor_info.set_rx_time(0x1122334455);

                //store updated settings
                updating_packet.set_packet_at(rx_anchor_info, anchor_number);

                auto ress = updating_packet.get_packet_at(1);
                
            }

            //got a packet from the last radio, flip frequencies + other housekeeping
            if(anchor_number == ANCHOR_NUM - 1) {
                is_freq_5 = !is_freq_5;
                set_channel_config(is_freq_5);
            }

            //our turn to send
            if((anchor_number + 1) % ANCHOR_NUM == ANCHOR_ID) {

                //copy all data from the updated packet to the outgoing packet just before we send it
                outgoing_packet = TokenRingPacket(updating_packet.get_compiled());



                //we only need to do this once per lap
                //new round, increment the sequence number
                if(ANCHOR_ID == 0) {
                    outgoing_packet.set_sequence_no(sequence_number + 1);
                } else {
                    outgoing_packet.set_sequence_no(sequence_number);
                }

                //debug printing
                if (0) {
                    // Serial.print("From: ");
                    // Serial.print(anchor_number);
                    // Serial.print(" Sequence: ");
                    // Serial.println(sequence_number);

                    //for that anchor, iterate through all anchors again
                    for(int j = 0; j < ANCHOR_NUM; ++j) {

                        //skip the main anchor
                        if(j == ANCHOR_ID) {
                            continue;
                        }

                        //print all data from the ranging packet from i to j
                        auto range_to_packet = outgoing_packet.get_packet_at(j);
                        Serial.print(range_to_packet.get_cir_real(), HEX);
                        Serial.print(",");
                        Serial.print(range_to_packet.get_cir_imaginary(), HEX);
                        Serial.print(",");
                        Serial.print(range_to_packet.get_phase_correction(), HEX);
                        Serial.print(",");
                        Serial.print(range_to_packet.get_preamble_accumulation(), HEX);
                        Serial.print(",");
                        Serial.print(range_to_packet.get_max_growth_cir(), HEX);
                        Serial.print(",");
                        print_u64(range_to_packet.get_rx_time(), HEX);
                        Serial.print(",");
                        Serial.print(j, HEX);
                        Serial.print(",");


                    }
                    Serial.print(outgoing_packet.get_sequence_no());
                    Serial.print(",");
                    Serial.println(outgoing_packet.get_index(), HEX);
                }



                UWBPacket outgoing = UWBPacket(
                    get_uuid(),
                    UWBPacket::BROADCAST_MAC,
                    PacketType::TokenRing,
                    outgoing_packet.get_compiled(),
                    outgoing_packet.get_compiled_len()
                );

                //todo: set tx delay here
                


                //last anchor in the series, we will be switching frequencies after this sends, so don't expect a packet back
                if(ANCHOR_ID == ANCHOR_NUM - 1) {

                    //set the outgoing time and give it to the radio

                    //We don't really need to do anything with the return value because the turnaround time should be fixed.
                    set_outgoing_time(TURNAROUND_HOP_TIME_US, rx_time);


                    //send packet without expected rx
                    auto send_result = send_packet(outgoing, DWT_START_TX_DELAYED); //DWT_START_TX_DELAYED

                    //failed to send on time, the radio was put into idle mode, so restart the whole thing
                    if(!send_result) {
                        Serial.println("Failed to send on time!");
                        return;
                    }
                    
                    //switch frequencies
                    is_freq_5 = !is_freq_5;
                    set_channel_config(is_freq_5);

                    //begin listening for the next packet
                    radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

                } else {
                    set_outgoing_time(TURNAROUND_TIME_US, rx_time);

                    //uint32_t tock = micros();
                    //Serial.print(tock - tick);
                    //with the NRF, the send delay is 3450 micros from get to this point

                    auto send_result = send_packet(outgoing, DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);

                    if(!send_result) {
                        Serial.println("Failed to send on time!");
                        return;
                    }

                    //to this point, it is ~4000 micros
                    //SPI takes most of this time. I need to go faster.

                }                
            } else {
                //not our turn, start listening for another
                radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }


        } else {
            //general errors and bad checksums
            while(0) {
                Serial.println("error: ");
                int sys_stat = radio->dwt_read32bitreg(SYS_STATUS_ID);
                Serial.println(sys_stat | (1 << 31), 2);
                Serial.println(SYS_STATUS_ALL_RX_ERR | (1 << 31), 2);
                Serial.print("Is Ch 5: ");
                Serial.println(is_freq_5);
                delay(1000);
            }

            //not sure if I should return or just clear status with general errors
            return; 
        }
    }



}

//custom tag loop
void loop_t_custom() {


    //start with clean slate
    radio->clear_system_status();

    //ensure frequency is set to 5 to begin with
    bool is_freq_5 = true;
    set_channel_config(is_freq_5);

    //stores all the gotten anchor data
    TokenRingPacket anchor_datas[ANCHOR_NUM] = {};

    //same with these, which are collected as we get each packet
    uint64_t rx_timestamps[ANCHOR_NUM] = {};
    uint32_t cir_reals[ANCHOR_NUM] = {};
    uint32_t cir_imgs[ANCHOR_NUM] = {};
    int32_t carrier_integrators[ANCHOR_NUM] = {};
    uint16_t phase_cals[ANCHOR_NUM] = {};
    uint32_t max_gcs[ANCHOR_NUM] = {}; //max growth CIR
    uint16_t rx_pcs[ANCHOR_NUM] = {}; //preamble accumulation count

    //a bodge to accommodate the slow serial printing, ensures we heard all anchors in at least one full round before printing the data.
    //I need to make the data transfer protocol more compact. I hesitate to go faster, but it might come to that too
    //this is adequate enough for testing though
    bool heard_anchor_0 = false;

    //more: With the code above, I'm only getting data from ch5 or ch9, not a mix, which is what I want. This is to force them to alternate.
    //Wait another packet if you have to, I need a channel mix!
    bool last_frequency_heard = is_freq_5;

    while(1) {
        //start listening
        radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

        auto rx_result = wait_for_message_with_timeout(0, true);

        //got successful packet
        if(rx_result == 1) {

            auto frame = get_packet();
            //get token ring from sender
			TokenRingPacket tr_packet = TokenRingPacket(frame.get_payload());

            uint8_t anchor_number = tr_packet.get_index();
            uint8_t sequence_number = tr_packet.get_sequence_no();


            //stow gotten packet
            anchor_datas[anchor_number] = TokenRingPacket(tr_packet.get_compiled());
            rx_timestamps[anchor_number] = radio->get_rx_timestamp_u64();

            if(anchor_number == 0) {
                // Serial.print(anchor_number);
                // Serial.print(" ");
                // print_u64(rx_timestamps[anchor_number], HEX);
                // Serial.println("");
                heard_anchor_0 = true;
            }

            //store important telemetry data
            {
                //lots of the values we need are now stored inside this struct
                dwt_rxdiag_t diagnostics = {};
                radio->dwt_readdiagnostics(&diagnostics);

                //1:1 with the DW1000 version
                //raw register: IP_DIAG_8.IP_FP
                uint16_t fp_index = diagnostics.ipatovFpIndex >> 6; //bit shifting removes the fractional part

                //higher resolution than the DW1000 version
                //raw register: IP_DIAG_12.IP_NACC
                rx_pcs[anchor_number] = diagnostics.ipatovAccumCount;
                
                //also read from: IP_DIAG_1, the value is 17 bits long
                max_gcs[anchor_number] = diagnostics.ipatovPower;

                //get phase of arrival, see page 180. This record is 14 bits long (in the DW1000, it is 7 bits long)
                phase_cals[anchor_number] = diagnostics.ipatovPOA;

                uint8_t complex_byte_len = 6;
                //extra 1 for the dummy leading byte we get when starting the read
                uint8_t cir_buffer[complex_byte_len * CIR_LEN + 1] = {};
                radio->dwt_readaccdata(cir_buffer, (complex_byte_len * CIR_LEN + 1), fp_index * complex_byte_len);

                //the original code took the second entry of three in the CIR buffer
                cir_reals[anchor_number] = cir_buffer[1 + complex_byte_len] //lo
                    | cir_buffer[1 + complex_byte_len + 1] << 8 //mid
                    | cir_buffer[1 + complex_byte_len + 2] << 16; //hi
                cir_imgs[anchor_number] = cir_buffer[1 + complex_byte_len * 2] //lo
                    | cir_buffer[1 + complex_byte_len * 2 + 1] << 8 //mid
                    | cir_buffer[1 + complex_byte_len * 2 + 2] << 16; //hi

                //get the carrier integrator, same as DW1000
                carrier_integrators[anchor_number] = radio->dwt_readcarrierintegrator();


            }

            //last anchor in the list, we got all the packets for this round
            if(anchor_number == ANCHOR_NUM - 1) {

                //conditional printing
                if(heard_anchor_0 && last_frequency_heard != is_freq_5) {

                    last_frequency_heard = is_freq_5;
                    heard_anchor_0 = false;
                    //auto tick = micros();
                    
                    //iterate through all anchors and dump their data to serial
                    Serial.print("A ");
                    Serial.println(is_freq_5);
                    for(int i = 0; i < ANCHOR_NUM; ++i) {

                        //for that anchor, iterate through all anchors again
                        for(int j = 0; j < ANCHOR_NUM; ++j) {

                            //skip the main anchor
                            if(j == i) {
                                continue;
                            }

                            //print all data from the ranging packet from i to j
                            auto range_to_packet = anchor_datas[i].get_packet_at(j);
                            Serial.print(range_to_packet.get_cir_real(), HEX);
                            Serial.print(",");
                            Serial.print(range_to_packet.get_cir_imaginary(), HEX);
                            Serial.print(",");
                            Serial.print(range_to_packet.get_phase_correction(), HEX);
                            Serial.print(",");
                            Serial.print(range_to_packet.get_preamble_accumulation(), HEX);
                            Serial.print(",");
                            Serial.print(range_to_packet.get_max_growth_cir(), HEX);
                            Serial.print(",");
                            print_u64(range_to_packet.get_rx_time(), HEX);
                            Serial.print(",");
                            Serial.print(j, HEX);
                            Serial.print(",");


                        }
                        Serial.print(anchor_datas[i].get_sequence_no());
                        Serial.print(",");
                        Serial.println(i, HEX);

                    }


                    //iterate through novel data and dump it to serial as well
                    Serial.println("T");
                    for(int i = 0; i < ANCHOR_NUM; ++i) {

                        Serial.print(cir_reals[i], HEX);
                        Serial.print(",");
                        Serial.print(cir_imgs[i], HEX);
                        Serial.print(",");
                        Serial.print(phase_cals[i], HEX);
                        Serial.print(",");
                        Serial.print(rx_pcs[i], HEX);
                        Serial.print(",");
                        Serial.print(max_gcs[i], HEX);
                        Serial.print(",");
                        print_u64(rx_timestamps[i], HEX);
                        Serial.print(",");
                        Serial.print(carrier_integrators[i], HEX);
                        
                        Serial.print(",");
                        Serial.print(anchor_datas[i].get_sequence_no());
                        Serial.print(",");
                        Serial.println(i, HEX);
                    }

                    //printing all this takes ~4.5 milliseconds
                    //it takes too long to hear the first anchor again after printing
                    //I need to swap raw printing for shooting out binary data
                    //auto tock = micros();
                    //Serial.println(tock - tick);
                }


                //switch frequencies
                is_freq_5 = !is_freq_5;
                set_channel_config(is_freq_5);


            }
        } else {
            //got some sort of error, re-initialize
            Serial.println("RX Error");
            return;
        }
    
    
    
    
    
    
    }


}




///////////////////////////////////////////////////////wrappers


//base
void loop_initiator() {
    //start with clean slate
    radio->clear_system_status();
    radio->dwt_forcetrxoff();

    //ensure frequency is set to 5
    bool is_freq_5 = true;
    set_channel_config(is_freq_5);

    //1000 microseconds * 1000, should be 1 second
    radio->dwt_setrxtimeout(UUS_TO_DWT_TIME * 1000000u);

    digitalWrite(LED_D9, false); //LED on 

    auto outgoing_packet = RangingPacket(0, 0, RangingFrameNum::Request);

    UWBPacket outgoing = UWBPacket(
        get_uuid(),
        UWBPacket::BROADCAST_MAC,
        PacketType::Ranging,
        outgoing_packet.get_compiled(),
        outgoing_packet.get_compiled_len()
    );
    send_packet(outgoing, DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);


    //wait for response
    auto response = 0;
    do {
        response = clone_check_for_rx();
    } while(response == 0);

    if(response == 1) {

        radio->clear_system_status();
        
        //Serial.println("Success");

        //dwt_rxdiag_t diagnostics = {};
        //radio->dwt_readdiagnostics(&diagnostics);
        //uint16_t fp_index = diagnostics.ipatovF3 >> 6;

        //test: compare these values
        //dwt_rxdiag_t diagnostics = {};
        //radio->dwt_readdiagnostics(&diagnostics);
        //uint16_t ip_poa = diagnostics.ipatovPOA;
        uint16_t ip_poa = radio->dwt_read16bitoffsetreg(IP_TOA_HI_ID, 1);
        //convert to signed
        int16_t ip_poa_signed = (int16_t)((ip_poa & 0x3FFF)|((ip_poa & 0x2000)?0xC000:0x0000));
        //float ip_poa_radians = (float)ip_poa_signed / (float)(1<<11);



        uint16_t fp_index_0 = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;

        //read in the data
        CirDebugPacket wave_data = CirDebugPacket();
        wave_data.read_acc_data(radio, fp_index_0 - (CirDebugPacket::VALUE_COUNT / 2));

        auto incoming = get_packet();
        CirDebugPacket remote_wave_data = CirDebugPacket(incoming.get_payload());
        auto carrier_integrator_0 = radio->dwt_readcarrierintegrator();

        //wait for post-final message
        radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);
        response = 0;
        do {
            response = clone_check_for_rx();
        } while(response == 0);

        if(response == 1) {





            uint16_t fp_index_1 = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;
            CirDebugPacket post_wave_data = CirDebugPacket();

            uint16_t fp_index_tag = remote_wave_data.get_acc_offset();
            auto carrier_integrator_tag = convert_u24_to_i24(remote_wave_data.get_carrier_integrator());

            int16_t ip_poa_tag = remote_wave_data.get_poa();



            post_wave_data.read_acc_data(radio, fp_index_1 - (CirDebugPacket::VALUE_COUNT / 2));
            auto carrier_integrator_1 = radio->dwt_readcarrierintegrator();

            Serial.println("A");
            print_cir_packet(wave_data);
            Serial.println();
            print_cir_packet(remote_wave_data);
            Serial.println();
            print_cir_packet(post_wave_data);

            Serial.println();
            Serial.print(carrier_integrator_0);
            Serial.print(",");
            Serial.print(carrier_integrator_tag);
            Serial.print(",");

            Serial.print(carrier_integrator_1);
            Serial.print(",");
            Serial.print(fp_index_tag);
            Serial.print(",");

            Serial.print(fp_index_0);
            Serial.print(",");
            Serial.print(fp_index_1);
            Serial.print(",");

            Serial.print(ip_poa_signed);
            Serial.print(",");
            Serial.print(ip_poa_tag);


            Serial.println();
            Serial.println("B");
        } else {
            Serial.print("RX2 Error: ");
            Serial.println(response);
        }





    } else {
        Serial.print("RX error: ");
        Serial.println(response);
    }

    digitalWrite(LED_D9, true); //LED off
    delay(50);

}


//tag
void loop_responder() {
    //start with clean slate
    radio->clear_system_status();

    //ensure frequency is set to 5
    bool is_freq_5 = true;
    set_channel_config(is_freq_5);

    //1000 microseconds * 1000, should be 1 second
    radio->dwt_setrxtimeout(UUS_TO_DWT_TIME * 1000000u);


    radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

    //wait for response
    auto response = 0;
    do {
        response = clone_check_for_rx();
    } while(response == 0);

    if(response == 1) {
        
        digitalWrite(LED_D9, false); //LED on 

        radio->clear_system_status();

        //todo: I'd rather read this direct
        //dwt_rxdiag_t diagnostics = {};
        //radio->dwt_readdiagnostics(&diagnostics);

        //1:1 with the DW1000 version
        //raw register: IP_DIAG_8.IP_FP
        uint16_t fp_index = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;
        //uint16_t fp_index = diagnostics.ipatovFpIndex >> 6; //bit shifting removes the fractional part

        //read in the data
        CirDebugPacket wave_data = CirDebugPacket();
        wave_data.read_acc_data(radio, fp_index - (CirDebugPacket::VALUE_COUNT / 2));

        //store carrier integrator for reverse compensation later
        wave_data.set_carrier_integrator(radio->dwt_readcarrierintegrator());
        //store the phase of arrival too
        wave_data.set_poa(radio->dwt_read16bitoffsetreg(IP_TOA_HI_ID, 1));


        UWBPacket outgoing = UWBPacket(
            get_uuid(),
            UWBPacket::BROADCAST_MAC,
            PacketType::Ranging,
            wave_data.get_compiled(),
            wave_data.get_compiled_len()
        );

        //.5 seconds = 500000
        set_outgoing_time(5000, radio->get_rx_timestamp_u64());
        if(send_packet(outgoing, DWT_START_TX_DELAYED)) {

            radio->clear_system_status();


            //do it again after another timespan
            set_outgoing_time(80000, radio->get_rx_timestamp_u64());
            if(send_packet(outgoing, DWT_START_TX_DELAYED)) {
                digitalWrite(LED_D9, true); //LED off
            }

        };



    } else {
        //Serial.print("RX error: ");
        //Serial.println(response);
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


    pinMode(LED_D9, OUTPUT);
    digitalWrite(LED_D9, true); //turn LED off

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

    Serial.println("Ready");
}


void loop() {

    
    //loop_test();
    //return;


#ifdef TAG
    loop_responder();
#else
    loop_initiator();
#endif

}















