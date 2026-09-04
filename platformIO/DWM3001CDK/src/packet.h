#pragma once

#include <Arduino.h>


//handles a generic UWB packet for ranging and data transfer.
//I'm trying to be IEEE 802.15.4-2015 compliant, so I'm limiting myself to 127 bytes total
//there's no real good reason for this, but it keeps things short and simple

//the types of packets we can send through the radio
typedef enum PacketType {
    Ok = 0,
    Err = 1,
    Ranging = 2,
    MultiRanging = 3,
    UserDefined = 4,
    TokenRing = 5,
} PacketType;

//used to identify what stage of the ranging process this frame is part of
typedef enum RangingFrameNum {
    Request = 0,
    Response = 1,
    Final = 2,
} RangingFrameNum;


typedef enum AnchorState {
    Listening = 0,
    Sending = 1,
} AnchorState;


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

//constructs and returns a ranging packet to be put into the greater packet's payload
class RangingPacket {

    public:
    
    //times are 40 bits long
    static const uint8_t TOTAL_LENGTH = 5 + 5 + 1;

    //see page 249 to see how these packets are structured
    static const uint8_t TIME_REPLY_U64_ID = 0; //the total time it took from getting a packet to sending out a response
    static const uint8_t TIME_ROUND_U64_ID = 5; //the round trip time for the first leg of the DSTWR (we don't need this for single-sided ranging)
    static const uint8_t FRAME_NO_ID = 10; //used to determine what stage of the ranging process we're in



    private:
    //packet format: [Time reply][Time round][frame number]
    uint8_t payload[TOTAL_LENGTH] = {};


    public:

    //construct from individual components
    RangingPacket(uint64_t time_reply, uint64_t time_round, RangingFrameNum frame_no) {

        //not 8-byte aligned, so we have to do bytewise copy
        PacketHelpers::num_to_byte_array(time_reply, payload + TIME_REPLY_U64_ID, 5);
        PacketHelpers::num_to_byte_array(time_round, payload + TIME_ROUND_U64_ID, 5);

        payload[FRAME_NO_ID] = frame_no;

    }

    //construct from byte array
    RangingPacket(const uint8_t* payload) {
        memcpy(this->payload, payload, TOTAL_LENGTH);
    }
    
    //return the rx time of arrival
    uint64_t get_reply_time() const {
        return PacketHelpers::byte_array_to_num(payload + TIME_REPLY_U64_ID, 5);
    }

    //return the tx time of transmission
    uint64_t get_round_time() const {
        return PacketHelpers::byte_array_to_num(payload + TIME_ROUND_U64_ID, 5);
    }
    
    //return the frame number
    RangingFrameNum get_frame_no() const {
        return (RangingFrameNum)payload[FRAME_NO_ID];
    }

    //return the whole flight-ready packet
    const uint8_t* get_compiled() const {
        return payload;
    }

    uint8_t get_compiled_len() const {
        return TOTAL_LENGTH;
    }

};




//part of the shared token ring of packets between the 4 anchors, there are three of 3 per transmission
//which cover all the anchors except the one sending the data
class AnchorInfoPacket {

    public:

    static const uint8_t CIR_REAL_ID = 0;
    static const uint8_t CIR_IMAGINARY_ID = 2;
    static const uint8_t PHASE_CORRECTION_ID = 4;
    static const uint8_t PREAMBLE_ACCUMULATION_ID = 5;
    static const uint8_t MAX_GROWTH_CIR_ID = 6;
    static const uint8_t RX_TIME_ID = 8;

    static const uint8_t TOTAL_LENGTH = 13;
    
    private:

    uint8_t payload[TOTAL_LENGTH] = {};

    public:


    //new empty packet
    AnchorInfoPacket()
    {}

    //populate packet with components
    AnchorInfoPacket(
        uint16_t cir_real,
        uint16_t cir_imaginary,
        uint8_t phase_correction,
        uint8_t preamble_accumulation,
        uint16_t max_growth_cir,
        uint64_t rx_time
    ) {
        PacketHelpers::num_to_byte_array(cir_real, payload + CIR_REAL_ID, 2);
        PacketHelpers::num_to_byte_array(cir_real, payload + CIR_IMAGINARY_ID, 2);
        payload[PHASE_CORRECTION_ID] = phase_correction;
        payload[PREAMBLE_ACCUMULATION_ID] = preamble_accumulation;
        PacketHelpers::num_to_byte_array(max_growth_cir, payload + MAX_GROWTH_CIR_ID, 2);
        PacketHelpers::num_to_byte_array(rx_time, payload + RX_TIME_ID, 5);
    }

    //from raw data
    AnchorInfoPacket(const uint8_t* payload) {
        memcpy(this->payload, payload, TOTAL_LENGTH);
    }

    //return the whole flight-ready packet
    const uint8_t* get_compiled() const {
        return payload;
    }

    uint8_t get_compiled_len() const {
        return TOTAL_LENGTH;
    }

    //get components
    uint16_t get_cir_real() const {
        return (uint16_t)PacketHelpers::byte_array_to_num(payload + CIR_REAL_ID, 2);
    }
    uint16_t get_cir_imaginary() const {
        return (uint16_t)PacketHelpers::byte_array_to_num(payload + CIR_IMAGINARY_ID, 2);
    }
    uint8_t get_phase_correction() const {
        return payload[PHASE_CORRECTION_ID];
    }
    uint8_t get_preamble_accumulation() const {
        return payload[PREAMBLE_ACCUMULATION_ID];
    }
    uint16_t get_max_growth_cir() const {
        return (uint16_t)PacketHelpers::byte_array_to_num(payload + MAX_GROWTH_CIR_ID, 2);
    }
    uint64_t get_rx_time() const {
        return PacketHelpers::byte_array_to_num(payload + RX_TIME_ID, 5);
    }




};

//this is the lumped version of the packet above.
class TokenRingPacket {

    public:

    static const uint8_t ANCHOR_INDEX_ID = 0;
    static const uint8_t SEQUENCE_NUM_ID = 1;
    static const uint8_t PAYLOAD_ID = 2;

    //we don't need the TX timestamp because the total held time is fixed for all anchors (rxtime + some constant)
    //allocate room for all anchor data - our anchor, + 1 for our anchor ID + 1 for our sequence number
    static const uint8_t TOTAL_LENGTH = PAYLOAD_ID + (ANCHOR_NUM - 1) * RangingPacket::TOTAL_LENGTH;
    
    private:

    uint8_t payload[TOTAL_LENGTH] = {};

    public:

    //construct new
    TokenRingPacket() {

    }

    //construct from raw
    TokenRingPacket(const uint8_t* compiled) {
        memcpy(this->payload, compiled, TOTAL_LENGTH);
    }

    //set what index this packet corresponds to
    //guards against setting an OOB number, returns true if it was set successfully
    bool set_index(uint8_t index) {
        //OOB guard
        if(index > (ANCHOR_NUM - 1)) {
            return false;
        }
        payload[ANCHOR_INDEX_ID] = index;
        return true;
    }

    //set what token loop sequence this is (typically increments each lap)
    void set_sequence_no(uint8_t sequence_num) {
        payload[SEQUENCE_NUM_ID] = sequence_num;
    }

    //get one of the bundled packets inside this one
    AnchorInfoPacket get_packet_at(uint8_t index) {
        //out of range, or matches the anchor this packet was transmitted from (because that's not included)
        if(index == get_index() || index >= ANCHOR_NUM) {
            //return empty one
            return AnchorInfoPacket();
        }

        //offset down by 1 since we skip our index
        if(index > get_index()) {
            index -= 1;
        }

        //pull the new anchor info out of the correct offset
        return AnchorInfoPacket(payload + PAYLOAD_ID + AnchorInfoPacket::TOTAL_LENGTH * index);

    }

    //get anchor index this packet was sent from
    uint8_t get_index() {
        return payload[ANCHOR_INDEX_ID];
    }

    //get the sequence number (that was set above)
    uint8_t get_sequence_no() {
        return payload[SEQUENCE_NUM_ID];
    }

    //return the whole flight-ready packet
    const uint8_t* get_compiled() const {
        return payload;
    }

    uint8_t get_compiled_len() const {
        return TOTAL_LENGTH;
    }

};


//defines the sector layout of a packet frame, should be 125 + 2 bytes long
class UWBPacket {

    private:
    /*
    [8 bytes         ][8 bytes    ][1 byte      ][1 byte     ][max. 107 bytes][2 bytes]
    [destination UUID][source UUID][payload type][payload len][payload       ][FCS    ]
    */

    //we'll favor storing things directly in the array instead of this way to eliminate any potential ambiguity with compiler settings
    //uint64_t dest_mac = 0;
    //uint64_t source_mac = 0;
    //uint8_t type = Ok;
    //uint8_t len = 0;
    //uint8_t payload[107] = {};
    //uint16_t fcs = 0; //checksum: this is auto-populated, but still counts toward the total 127 bytes

    //offsets in the payload for each piece of data
    static const uint8_t SRC_UUID_ID = 0;
    static const uint8_t DEST_UUID_ID = 8;
    static const uint8_t PACKET_TYPE_ID = 16;
    static const uint8_t PAYLOAD_LEN_ID = 17;
    static const uint8_t PAYLOAD_ID = 18;

    //auto-removed the last two bytes for the FCS, we we get 125
    static const uint8_t TOTAL_LENGTH = 125;

    //where all the bytes go
    uint8_t compiled[TOTAL_LENGTH] = {};

    public:

    //all UUIDs should accept a packet with this identifier
    static const uint64_t BROADCAST_MAC = 0x0;

    //takes: source and destination addresses
    UWBPacket(uint64_t source_mac, uint64_t dest_mac) {
        set_addresses(source_mac, dest_mac);
    }

    UWBPacket(uint64_t source_mac, uint64_t dest_mac, PacketType packet_type, const uint8_t* payload, uint8_t payload_len) {
        set_addresses(source_mac, dest_mac);
        set_packet_type(packet_type);
        set_payload(payload, payload_len);
    }

    //takes: raw compiled frame and compiled length
    UWBPacket(uint8_t* compiled, uint8_t compiled_len) {
        if(compiled_len <= TOTAL_LENGTH) {
            memcpy(this->compiled, compiled, compiled_len);
        }
    }

    //the empty constructor
    UWBPacket() {

    }



    //get the length of the payload
    uint8_t get_payload_len() const {
        return compiled[PAYLOAD_LEN_ID];
    }
    //get length of the header
    uint8_t get_header_len() const {
        return 8 + 8 + 1 + 1;
    }
    //get the total length of the packet after a payload of some size has been added
    //this is the size you use when sending the compiled_packet off to the radio
    uint8_t get_compiled_len() const {
        return get_header_len() + get_payload_len();
    }
    
    //return constant reference to finished packet
    const uint8_t* get_compiled() {
        return compiled;
    }
    //return constant reference to just the payload of the packet
    const uint8_t* get_payload() {
        return compiled + PAYLOAD_ID; 
    }

    //return type of the packet
    PacketType get_packet_type() {
        return (PacketType)compiled[PACKET_TYPE_ID];
    }

    //get the long source address
    uint64_t get_source_uuid() {
        return PacketHelpers::byte_array_to_num(compiled + SRC_UUID_ID, 8);
    }
    //get the long destination address
    uint64_t get_dest_uuid() {
        return PacketHelpers::byte_array_to_num(compiled + DEST_UUID_ID, 8);
    }


    //copies the payload into the internal payload holder, setting payload length and other things
    bool set_payload(const uint8_t* payload, uint8_t len) {
        //exceeds allowed payload size, don't add it in
        if(len > (TOTAL_LENGTH - get_header_len())) {
            return false;
        }

        //copy it in and set the length
        memcpy(compiled + PAYLOAD_ID, payload, len);
        compiled[PAYLOAD_LEN_ID] = len;

        return true;
    }

    //set the type of the packet
    void set_packet_type(PacketType type) {
        compiled[PACKET_TYPE_ID] = type;
    }

    void set_addresses(uint64_t source_mac, uint64_t dest_mac) {
        //stow in byte array
        PacketHelpers::num_to_byte_array(source_mac, compiled + SRC_UUID_ID, 8);
        PacketHelpers::num_to_byte_array(dest_mac, compiled + DEST_UUID_ID, 8);
    }


};



