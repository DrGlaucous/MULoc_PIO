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
} PacketType;

//used to identify what stage of the ranging process this frame is part of
typedef enum RangingFrameNum {
    Request = 0,
    Response = 1,
    Final = 2,
} RangingFrameNum;


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

//holds several ranging packets in a single structure for a one-to-many broadcast
class MultiRangingPacket {

    public:
    
    //max number of ranging packets we can store in a single multiranging packet
    static const uint8_t MAX_PACKETS = 5;

    //length of a ranging packet + the length of the UUID it's tied to
    static const uint8_t LUMPED_LENGTH = (8 + RangingPacket::TOTAL_LENGTH);

    //length of the size byte + length of the maximum packets
    static const uint8_t TOTAL_LENGTH = 1 + LUMPED_LENGTH * MAX_PACKETS;

    //offsets in the compiled array
    static const uint8_t PACKET_COUNT_ID = 0;
    static const uint8_t PACKET_DATA_ID = 1;


    //structure:
    //[packet count][dest mac][packet][dest mac 2][packet 2][etc...]


    private:

    uint8_t payload[TOTAL_LENGTH] = {};

    public:

    //new packet
    MultiRangingPacket() {

    }


    //construct from raw payload
    MultiRangingPacket(const uint8_t* compiled, uint8_t compiled_len) {
        if(compiled_len <= TOTAL_LENGTH) {
            memcpy(this->payload, compiled, compiled_len);
        }
    }

    //get number of ranging packets in the data
    uint8_t get_packet_count() const {
        return payload[PACKET_COUNT_ID];
    }

    //returns the packet at this index
    RangingPacket get_packet_at(uint8_t index) {
        
        //OOB, return a "0" ranging packet
        if(index >= get_packet_count()) {
            return RangingPacket(0, 0, RangingFrameNum::Request);
        }


        auto offset = payload + PACKET_DATA_ID + (index * LUMPED_LENGTH) + 8;
        return RangingPacket(offset);


    }

    //returns the mac at this index
    uint64_t get_mac_at(uint8_t index) {

        //OOB, return nothing
        if(index >= get_packet_count()) {
            return 0;
        }

        auto offset = payload + PACKET_DATA_ID + (index * LUMPED_LENGTH);

        uint64_t item = PacketHelpers::byte_array_to_num(offset, 8);

        return item;
    }


    //get pointer to the compiled data
    const uint8_t* get_compiled() const {
        return payload;
    }

    //get length of the outgoing packet
    uint8_t get_compiled_len() const {
        return get_packet_count() * LUMPED_LENGTH + 1;
    }

    //puts a new packet into the ranging data
    bool add_packet(const RangingPacket& packet, uint64_t mac_address) {

        //out of room
        if(payload[PACKET_COUNT_ID] >= MAX_PACKETS) {
            return false;
        }

        //where the next packet should go
        auto offset = payload + PACKET_DATA_ID + (payload[PACKET_COUNT_ID] * LUMPED_LENGTH);

        //add the destination MAC
        //uint64_t* boffset = (uint64_t*)offset;
        //*boffset = mac_address; //breaks the program because it's not 8-byte aligned
        //memcpy(offset, &mac_address, sizeof(mac_address)); //endian-dependant, use the method below
        PacketHelpers::num_to_byte_array(mac_address, offset, 8);


        offset += 8; //move offset past the MAC

        //put packet into the next slot
        memcpy(offset, packet.get_compiled(), packet.get_compiled_len());

        //increment count
        payload[PACKET_COUNT_ID] += 1;

        return true;

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



