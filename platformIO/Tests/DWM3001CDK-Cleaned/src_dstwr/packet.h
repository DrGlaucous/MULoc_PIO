#pragma once

#include <Arduino.h>


//handles a generic UWB packet for ranging and data transfer.
//I'm trying to be IEEE 802.15.4-2015 compliant, so I'm limiting myself to 127 bytes total


//the types of packets we can send through the radio
typedef enum PacketType {
    Ok = 0,
    Err = 1,
    Ranging = 2, 
    UserDefined = 3,   
} PacketType;

//constructs and returns a ranging packet to be put into the greater packet's payload
class RangingPacket {

    private:

    static const uint8_t TOTAL_LENGTH = 8 + 8 + 1;

    //see page 249 to see how these packets are structured
    static const uint8_t TIME_REPLY_U64_ID = 0; //the total time it took from getting a packet to sending out a response
    static const uint8_t TIME_ROUND_U64_ID = 8; //the round trip time for the first leg of the DSTWR (we don't need this for single-sided ranging)
    static const uint8_t FRAME_NO_ID = 16; //used to determine what stage of the ranging process we're in


    //packet format: [Time reply][Time round][frame number]



    uint8_t payload[TOTAL_LENGTH] = {};


    public:
    //construct from individual components
    RangingPacket(uint64_t time_reply, uint64_t time_round, uint8_t frame_no) {

        uint64_t* time_reply_ptr = (uint64_t*)(payload + TIME_REPLY_U64_ID);
        uint64_t* time_round_ptr = (uint64_t*)(payload + TIME_ROUND_U64_ID);
        
        *time_reply_ptr = time_reply;
        *time_round_ptr = time_round;

        payload[FRAME_NO_ID] = frame_no;

    }

    //construct from byte array
    RangingPacket(const uint8_t* payload) {
        memcpy(this->payload, payload, TOTAL_LENGTH);
    }
    
    //return the rx time of arrival
    uint64_t get_reply_time() const {
        uint64_t* time_reply_ptr = (uint64_t*)(payload + TIME_REPLY_U64_ID);
        return *time_reply_ptr;
    }

    //return the tx time of transmission
    uint64_t get_round_time() const {
        uint64_t* time_round_ptr = (uint64_t*)(payload + TIME_ROUND_U64_ID);
        return *time_round_ptr;
    }
    
    //return the frame number
    uint8_t get_frame_no() const {
        return payload[FRAME_NO_ID];
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
        return (8 * 2) + (2);
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
        uint64_t* source_offset = (uint64_t*)&compiled[SRC_UUID_ID];
        return *source_offset;
    }
    //get the long destination address
    uint64_t get_dest_uuid() {
        uint64_t* dest_offset = (uint64_t*)&compiled[DEST_UUID_ID];
        return *dest_offset;
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
        //cast packet to 8 byte ints
        uint64_t* source_offset = (uint64_t*)&compiled[SRC_UUID_ID];
        uint64_t* dest_offset = (uint64_t*)&compiled[DEST_UUID_ID];

        //set them accordingly
        *source_offset = source_mac;
        *dest_offset = dest_mac;


    }


};



