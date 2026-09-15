#pragma once

#include <Arduino.h>
#include "dw3000.h"
#include "dw3000_regs.h"
#include "dw3000_shared_defines.h"

#include "frame_header.h"
#include "common_header.h"


// Device configuration
//#define RX_NODE
#define TX_NODE

#ifdef RX_NODE
#define SHORT_ADDR 0x0002
#endif

#ifdef TX_NODE
#define SHORT_ADDR 0x0001
#define LCD_ENABLE
#endif

// Anchor configuration
// Number of anchors used in the system
#define ANCHOR_NUM 4
// Anchor ID of the current anchor
#define ANCHOR_ID 3

#define ANCHOR_LISTEN 0
#define ANCHOR_SEND 2

#define RX_ANT_DLY 0
#define TX_ANT_DLY 32880


/* Payload format (CIR and receiving timestamps of messages from other anchors)

|---CIR(4 byte)---|--RxTime(5 byte)--|******|---CIR(4 byte)---|--RxTime(5 byte)--|-IDX(1 byte)-|
|---------------------------13 byte * (Anchor Number-1)--------------------------|----1 byte---| 
*/

//13 bytes total
//[2 bytes ][2 bytes][1 byte          ][1 byte               ][2 bytes       ][5 bytes]
//[CIR real][CIR img][phase correction][preamble accumulation][max growth cir][rx time]

//DW3000: 21 bytes total
//[4 bytes ][4 bytes][2 byte          ][2 byte               ][4 bytes       ][5 bytes]
//[CIR real][CIR img][phase correction][preamble accumulation][max growth cir][rx time]

//length of a single set of data from an anchor
#define SINGLE_LEN 13


#define POA_LEN 4

#define END_LEN 0

/* Length of the common part of the message (up to and including the function code, see NOTE 2 below). */
//the prefix header present on all messages (index 8 is used as the anchor id)
//the header is: msg_common[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21};
#define ALL_MSG_COMMON_LEN 10

/* Indexes to access some of the fields in the frames defined above. */

//all message sequence index (where the frame is in the TWR sequence)
#define ALL_MSG_SN_IDX 2

#define SENDING_TX_TS_IDX 10



/* Delay between frames, in UWB microseconds. */
#define DELAY_TIME 600
#define DELAY_TIME_TURN 600
#define RX_AFTER_TX_DELAY 450
#define RX_TIMEOUT 2500

/* Length of channel impulse response to read from the accumulator buffer*/
//each complex/real value is 3 bytes long (6 for the whole number). Maybe that's why it's like this?
//Ans: no, with the DW1000, each complex value is 2+2 bytes long. I think this just means we want 3 of these.
#define CIR_LEN 3

#define NET_PANID 0xF0F2

extern int psduLength ;
extern srd_msg_dsss msg_f_send ;
//extern srd_msg_dsss msg_f_send2 ;

#ifndef SPEED_OF_LIGHT
#define SPEED_OF_LIGHT      (299702547.0)  // in m/s in air
#endif

#ifndef FRAME_LEN_MAX
#define FRAME_LEN_MAX 127
#endif

/* UWB microsecond (uus) to device time unit (dtu, around 15.65 ps) conversion factor.
 * 1 uus = 512 / 499.2 ? and 1 ? = 499.2 * 128 dtu. */
#define UUS_TO_DWT_TIME 65536

#define PRE_TIMEOUT 0


///////////////////////////////////more constants

#define BAUD_RATE 460800

#define LED_D9 4
#define LED_D10 5
#define LED_D11 22
#define LED_D12 14

//onboard pushbuttons for the DWM3001CDK
#define SWITCH_1 18
#define SWITCH_2 2

//adding 32 gives us pin bank 2.
#define SPI_CS 32 + 6
#define SPI_CLK 3
#define SPI_MOSI 8
#define SPI_MISO 29

//other radio control pins
#define DW_RST 25
#define DW_IRQ 32 + 2
#define DW_WUP 32 + 19


#define POLL_TX_TO_RESP_RX_DLY_UUS 450
#define RESP_RX_TO_FINAL_TX_DLY_UUS 600
#define POLL_RX_TO_RESP_TX_DLY_UUS 600
#define RESP_TX_TO_FINAL_RX_DLY_UUS 600


#define FINAL_MSG_TYPE_IDX 0
#define FINAL_MSG_CM_100S_IDX 1
#define FINAL_MSG_CM_1S_IDX 2


#define FINAL_MSG_POLL_TX_TS_IDX 3
#define FINAL_MSG_RESP_RX_TS_IDX 8
#define FINAL_MSG_FINAL_TX_TS_IDX 13



class PacketHelpers {
    public:

    //copy a number into a char array, endian independent
    //in alignment with network standards, bytes will be ordered from MSB to LSB,
    //"count" number of bytes from the input number will be copied over, starting with its least significant bit
    //so if the input is 0xFF00112233445566 and count is 2, then  bytes [55][66] will be copied over
    //note: we do not do sign extension!
    static void num_to_byte_array(uint64_t number, uint8_t* ptr, uint8_t count) {
        
        //start at far end and work backwards
        for(int i = count - 1; i >= 0; --i) {
            //put LSB into far slot
            ptr[i] = number & 0xFF;
            //shift everything over
            number >>= 8;
        }
    }

    //the reverse of above, get a number from a byte array (largely ripped from get_rx_timestamp_u64)
    //except that one is in reverse order due to how the registers are read [lowest---highest]. My bytes are stored [MSB---LSB]
    static uint64_t byte_array_to_num(const uint8_t* ptr, uint8_t count) {

        uint64_t output = 0;
        for(int i = 0; i < count; ++i) {
            output <<= 8;
            output |= ptr[i];
        }
        return output;
    }

};



