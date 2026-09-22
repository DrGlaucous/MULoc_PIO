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

    //force into ILDE_RC
    radio->dwt_forcetrxoff();

    //enable pll prebuffer (required for channel 5 to properly lock to the incoming phase)
    //page 42 of the API guide outlines this method, it says it should be changed in IDLE_RC mode, but putting int below the dwt_configure() calls below didn't seem to hurt anything
    radio->dwt_setpllrxprebufen(dwt_pll_prebuf_cfg_e::DWT_PLL_RX_PREBUF_ENABLE);

    //force it to log important telemetry
    radio->dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);

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
}

//set the time at which the next transaction should happen
//takes the time in microseconds until the next transaction, returns the scheduled timestamp in radio units
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



bool is_freq_5 = true;

//tag
void loop_initiator() {
    //start with clean slate
    radio->clear_system_status();
    radio->dwt_forcetrxoff();

    //ensure frequency is set correctly
    set_channel_config(is_freq_5);

    //1000 microseconds * 1000, should be 1 second
    radio->dwt_setrxtimeout(UUS_TO_DWT_TIME * 1000000u);

    digitalWrite(LED_D9, false); //LED on 

    //kick off poll
    auto poll_ranging = RangingPacket(0, 0, RangingFrameNum::Request);
    UWBPacket poll_packet = UWBPacket(
        get_uuid(),
        UWBPacket::BROADCAST_MAC,
        PacketType::Ranging,
        poll_ranging.get_compiled(),
        poll_ranging.get_compiled_len()
    );
    send_packet(poll_packet, DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

    auto poll_tx_timestamp = radio->get_tx_timestamp_u64();

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
        //uint16_t fp_index = diagnostics.ipatovFpIndex >> 6;
        //test: compare these values
        //dwt_rxdiag_t diagnostics = {};
        //radio->dwt_readdiagnostics(&diagnostics);
        //uint16_t ip_poa = diagnostics.ipatovPOA;

        //read phase of arrival and convert to signed
        uint16_t ip_poa = radio->dwt_read16bitoffsetreg(IP_TOA_HI_ID, 1);
        //int16_t ip_poa_signed = (int16_t)((ip_poa & 0x3FFF)|((ip_poa & 0x2000)?0xC000:0x0000));
        //float ip_poa_radians = (float)ip_poa_signed / (float)(1<<11); //we'll do this in python


        //read in the cir data
        uint16_t fp_index = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;
        CirDebugPacket final_cirdebug = CirDebugPacket();
        final_cirdebug.read_acc_data(radio, fp_index - (CirDebugPacket::VALUE_COUNT / 2));

        final_cirdebug.set_poa(ip_poa);
        final_cirdebug.set_acc_offset(fp_index);
        final_cirdebug.set_carrier_integrator(radio->dwt_readcarrierintegrator());

        //calculate final packet outgoing time
        auto final_tx_timestamp = set_outgoing_time(TURNAROUND_TIME_US, radio->get_rx_timestamp_u64());
        auto held_time = final_tx_timestamp - poll_tx_timestamp;
        final_cirdebug.set_held_time((uint32_t)held_time);

        auto final_packet = UWBPacket(
            get_uuid(),
            UWBPacket::BROADCAST_MAC,
            PacketType::Ranging,
            final_cirdebug.get_compiled(),
            final_cirdebug.get_compiled_len()
        );


        //send final
        if(send_packet(final_packet, DWT_START_TX_DELAYED)) {

            radio->clear_system_status();
            set_outgoing_time(TURNAROUND_TIME_US, radio->get_tx_timestamp_u64());

            //send post-final
            if(send_packet(final_packet, DWT_START_TX_DELAYED)) {
                radio->clear_system_status();

                //send post-post-POST final (wow)
                set_outgoing_time(TURNAROUND_TIME_US, radio->get_tx_timestamp_u64());
                if(send_packet(final_packet, DWT_START_TX_DELAYED)) {

                    //send pppf
                    set_outgoing_time(TURNAROUND_TIME_US, radio->get_tx_timestamp_u64());
                    if(send_packet(final_packet, DWT_START_TX_DELAYED)) {
    
                        digitalWrite(LED_D9, false);
    
                    } else {
                        Serial.println("Send P3-Final Error");
                    }


                } else {
                    Serial.println("Send Post-post-Final Error");
                }
            } else {
                Serial.println("Send Post-Final Error");
            }


        } else {
            Serial.println("Send Final Error");
        }

    }

    digitalWrite(LED_D9, true); //LED off
    delay(200);

}


//base
void loop_responder() {


    //start with clean slate
    radio->clear_system_status();

    //ensure frequency is set to 5
    set_channel_config(is_freq_5);

    //1000 microseconds * 1000, should be 1 second
    radio->dwt_setrxtimeout(UUS_TO_DWT_TIME * 1000000u);


    radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

    //wait for response
    auto poll_status = 0;
    do {
        poll_status = clone_check_for_rx();
    } while(poll_status == 0);

    if(poll_status == 1) {
        
        digitalWrite(LED_D9, false); //LED on 

        auto poll_rx_timestamp = radio->get_rx_timestamp_u64();
        auto poll_packet = get_packet();
        auto poll_cirdebug = CirDebugPacket(poll_packet.get_payload());


        radio->clear_system_status();

        //todo: I'd rather read this direct
        //dwt_rxdiag_t diagnostics = {};
        //radio->dwt_readdiagnostics(&diagnostics);

        //1:1 with the DW1000 version
        //raw register: IP_DIAG_8.IP_FP
        //uint16_t fp_index = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;


        //populate outgoing packet with data (not really needed; the other radio doesn't read this)
        uint16_t fp_index = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;
        CirDebugPacket poll_gleaned_info = CirDebugPacket();
        poll_gleaned_info.read_acc_data(radio, fp_index - (CirDebugPacket::VALUE_COUNT / 2));
        poll_gleaned_info.set_carrier_integrator(radio->dwt_readcarrierintegrator()); //store carrier integrator for reverse compensation later
        poll_gleaned_info.set_poa(radio->dwt_read16bitoffsetreg(IP_TOA_HI_ID, 1)); //store the phase of arrival too


        UWBPacket response_packet = UWBPacket(
            get_uuid(),
            UWBPacket::BROADCAST_MAC,
            PacketType::Ranging,
            poll_gleaned_info.get_compiled(),
            poll_gleaned_info.get_compiled_len()
        );

        set_outgoing_time(TURNAROUND_TIME_US, radio->get_rx_timestamp_u64());


        if(send_packet(response_packet, DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED)) {

            radio->clear_system_status();

            auto final_status = 0;
            do {
                final_status = clone_check_for_rx();
            } while(final_status == 0);

            if(final_status == 1) {
                
                radio->clear_system_status();
                
                //save the response CIR data
                auto final_rx_timestamp = radio->get_rx_timestamp_u64();
                auto final_packet = get_packet();
                auto response_gleaned_info = CirDebugPacket(final_packet.get_payload());

                //save the final CIR data
                CirDebugPacket final_gleaned_info = CirDebugPacket();
                fp_index = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;
                final_gleaned_info.read_acc_data(radio, fp_index - (CirDebugPacket::VALUE_COUNT / 2));
                final_gleaned_info.set_carrier_integrator(radio->dwt_readcarrierintegrator()); //store carrier integrator for reverse compensation later
                final_gleaned_info.set_poa(radio->dwt_read16bitoffsetreg(IP_TOA_HI_ID, 1)); //store the phase of arrival too
                

                radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);
                auto post_final_status = 0;
                do {
                    post_final_status = clone_check_for_rx();
                } while(post_final_status == 0);

                if(post_final_status == 1) {

                    radio->clear_system_status();


                    auto post_final_rx_timestamp = radio->get_rx_timestamp_u64();
                    auto post_final_packet = get_packet();

                    //save the post final CIR data
                    CirDebugPacket post_final_cirdebug = CirDebugPacket();
                    fp_index = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;
                    post_final_cirdebug.read_acc_data(radio, fp_index - (CirDebugPacket::VALUE_COUNT / 2));
                    post_final_cirdebug.set_carrier_integrator(radio->dwt_readcarrierintegrator()); //store carrier integrator for reverse compensation later
                    post_final_cirdebug.set_poa(radio->dwt_read16bitoffsetreg(IP_TOA_HI_ID, 1)); //store the phase of arrival too
                    

                    radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    auto post_post_final_status = 0;
                    do {
                        post_post_final_status = clone_check_for_rx();
                    } while(post_post_final_status == 0);

                    if(post_post_final_status == 1) {



                        radio->clear_system_status();

                        
                        //save the ppf CIR data
                        CirDebugPacket post_post_final_cirdebug = CirDebugPacket();
                        fp_index = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;
                        post_post_final_cirdebug.read_acc_data(radio, fp_index - (CirDebugPacket::VALUE_COUNT / 2));
                        post_post_final_cirdebug.set_carrier_integrator(radio->dwt_readcarrierintegrator()); //store carrier integrator for reverse compensation later
                        post_post_final_cirdebug.set_poa(radio->dwt_read16bitoffsetreg(IP_TOA_HI_ID, 1)); //store the phase of arrival too


                        radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);
                        auto p3f_status = 0;
                        do {
                            p3f_status = clone_check_for_rx();
                        } while(p3f_status == 0);
                        if(p3f_status == 1) {

                            //save the pppf CIR data
                            CirDebugPacket p3f_cirdebug = CirDebugPacket();
                            fp_index = radio->dwt_read16bitoffsetreg(IP_DIAG_8_ID, 0) >> 6;
                            p3f_cirdebug.read_acc_data(radio, fp_index - (CirDebugPacket::VALUE_COUNT / 2));
                            p3f_cirdebug.set_carrier_integrator(radio->dwt_readcarrierintegrator()); //store carrier integrator for reverse compensation later
                            p3f_cirdebug.set_poa(radio->dwt_read16bitoffsetreg(IP_TOA_HI_ID, 1)); //store the phase of arrival too



                            digitalWrite(LED_D9, true); //LED off 

                            //held time by remote
                            uint32_t remote_held_time = response_gleaned_info.held_time();
                            //same time by local
                            uint32_t local_held_time = final_rx_timestamp - poll_rx_timestamp;

                            Serial.println("A");
                            print_cir_packet(poll_gleaned_info);
                            Serial.println();
                            print_cir_packet(response_gleaned_info);
                            Serial.println();
                            print_cir_packet(final_gleaned_info);
                            Serial.println();
                            print_cir_packet(post_final_cirdebug);
                            Serial.println();
                            print_cir_packet(post_post_final_cirdebug);
                            Serial.println();
                            print_cir_packet(p3f_cirdebug);


                            Serial.println();
                            Serial.print(poll_gleaned_info.get_carrier_integrator());
                            Serial.print(",");
                            Serial.print(final_gleaned_info.get_carrier_integrator());
                            Serial.print(",");

                            Serial.print(post_final_cirdebug.get_carrier_integrator());
                            Serial.print(",");
                            Serial.print(post_post_final_cirdebug.get_carrier_integrator());
                            Serial.print(",");


                            Serial.print(remote_held_time);
                            Serial.print(",");
                            Serial.print(local_held_time);
                            // Serial.print(",");

                            // Serial.print(fp_index_0);
                            // Serial.print(",");
                            // Serial.print(fp_index_1);
                            // Serial.print(",");

                            // Serial.print(ip_poa_signed);
                            // Serial.print(",");
                            // Serial.print(ip_poa_tag);


                            Serial.println();
                            Serial.println("B");


                        }





                    }
                    else {
                        Serial.print("PPFS Error: ");
                        Serial.println(post_post_final_status);
                    }




                } else {
                    Serial.print("RX Error: ");
                    Serial.println(post_final_status);
                }


            } else {
                Serial.print("RX Error: ");
                Serial.println(final_status);
            }


        } else {
            Serial.print("TX Error");
        }



    } else {
        Serial.print("RX error: ");
        Serial.println(poll_status);
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
    loop_initiator();
#else
    loop_responder();
#endif

}















