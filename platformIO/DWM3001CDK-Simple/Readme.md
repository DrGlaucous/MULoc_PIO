
I need to take two steps back first before continuing with the greater MULOC.
I want to see if I can do the initial paper's proposal of extracting phase angle between just two tags that do DSTWR + 1 signals






---

I've uploaded my embedded arduino code and the paper I'm trying to replicate. I'm also looking at https://github.com/MULoc/MULoc to get some insight on how the researchers for the paper implemented some of the data gathering required for phase estimation. Unlike the paper and the MULOC code, I'm using a DW3000 radio module, so some of the registers are different from the DW1000 they use.

My code aims specifically to reproduce section 4 of the research paper and get a stable phase value of my radio pairs. I extract what I think is the CIR from each radio and then print them on the anchor side to compare their values. I think the paper says that the CIR phase angle from each radio will roughly cancel out when compared and added, but when I add them, they don't do this at all, and are seemingly still random.

Looking at the raw output from both the local phase and the remote phase doesn't look anything like figure 10 in the paper. (where even before coarse-grained cancellation, the phases appeared as a rough mirror of each other with a vertical offset) My results don't seem to have have any correlation at all.


I haven't even tried to implement the fine-grained cancellation yet, I just want to ensure coarse-grained cancellation is working.

The muloc code and processing steps have some extra stuff included, but if I understand what's happening correctly, the complex phase value is read out with:
```c
uint16 fp_index = dwt_read16bitoffsetreg(RX_TIME_ID, RX_TIME_FP_INDEX_OFFSET) >> 6;
dwt_readaccdata(cir_buffer,
    CIR_LEN * 4 + 1,
    (fp_index) * 4
);
```


My code does it with:
```cpp

dwt_rxdiag_t diagnostics = {};
radio->dwt_readdiagnostics(&diagnostics);

uint16_t fp_index = diagnostics.ipatovFpIndex >> 6; //bit shifting removes the fractional part

uint8_t complex_byte_len = 6;
//extra 1 for the dummy leading byte we get when starting the read
uint8_t cir_buffer[complex_byte_len * CIR_LEN + 1] = {};
radio->dwt_readaccdata(cir_buffer, (complex_byte_len * CIR_LEN + 1), fp_index);

//the original code took the second entry of three in the CIR buffer
cir_real =
        cir_buffer[1 + 1*complex_byte_len] //lo
    | cir_buffer[1 + 1*complex_byte_len + 1] << 8 //mid
    | cir_buffer[1 + 1*complex_byte_len + 2] << 16; //hi
cir_img =
        cir_buffer[1 + 1*complex_byte_len + 3] //lo
    | cir_buffer[1 + 1*complex_byte_len + 4] << 8 //mid
    | cir_buffer[1 + 1*complex_byte_len + 5] << 16; //hi


```

for `radio->dwt_readaccdata(cir_buffer, (complex_byte_len * CIR_LEN + 1), fp_index);`, I originally tried multiplying `fp_index` by 6 just like how the original code multiplies it by 4, but if I understand the DW3000 documentation correctly, I don't need to do this on the newer radio. (trying either/or still results in random noise on the output)


Is there anything I'm missing here?


For additional context, I'm using my custom fork of the makerfabs DW3000 library. My fork is found here:
https://github.com/DrGlaucous/DW3000


---

The CIR data *is* supposed to be 24 bits long, according to the documentation in the official DW3000 user manual:

> Accessing the accumulator CIR memory is a little different than accessing other register files in two ways:  
Firstly for every SPI read access of the accumulator memory, a single dummy octet is output before the first 
byte of valid accumulator data.  Secondly the offset specified in the SPI transaction is not the byte index, but 
instead is the sample index, where each accumulator sample is an complex value provided as a 24-bit (3
octet) real value followed by a 24-bit (3-octet) imaginary value. Each value is actually 18-bit precision, with 
the upper 6-bits being all zero or ones depending on the sign of the value. 

This also confirms I do not need to multiply the `fp_index` by 6.


To verify things further, I dumped the 42 entries on either side of `fp_index` using this code:
```cpp
uint32_t cir_real = 0;
uint32_t cir_img = 0;
{
    dwt_rxdiag_t diagnostics = {};
    radio->dwt_readdiagnostics(&diagnostics);

    uint16_t fp_index = diagnostics.ipatovFpIndex >> 6; //bit shifting removes the fractional part

    uint8_t complex_byte_len = 6;
    //extra 1 for the dummy leading byte we get when starting the read
    uint8_t cir_buffer[complex_byte_len * CIR_LEN + 1] = {};
    radio->dwt_readaccdata(cir_buffer, (complex_byte_len * CIR_LEN + 1), fp_index);

    //the original code took the second entry of three in the CIR buffer
    cir_real =
            cir_buffer[1 + 1*complex_byte_len] //lo
        | cir_buffer[1 + 1*complex_byte_len + 1] << 8 //mid
        | cir_buffer[1 + 1*complex_byte_len + 2] << 16; //hi
    cir_img =
            cir_buffer[1 + 1*complex_byte_len + 3] //lo
        | cir_buffer[1 + 1*complex_byte_len + 4] << 8 //mid
        | cir_buffer[1 + 1*complex_byte_len + 5] << 16; //hi





    //TEST: dump a sizeable portion of the CIR buffer to the serial terminal
    {
        //tests
        const size_t debug_sample_size = 42;
        float magnitude_bulk[debug_sample_size] = {};
        float phase_bulk[debug_sample_size] = {};
        uint8_t cir_buffer_bulk[complex_byte_len * debug_sample_size + 1] = {};
        radio->dwt_readaccdata(cir_buffer_bulk, (complex_byte_len * debug_sample_size + 1), fp_index - (debug_sample_size / 2));

        //convert those into complex numbers
        for(size_t i = 0; i < debug_sample_size; ++i) {
            uint32_t cir_real_2 =
                cir_buffer_bulk[1 + i*complex_byte_len] //lo
                | cir_buffer_bulk[1 + i*complex_byte_len + 1] << 8 //mid
                | cir_buffer_bulk[1 + i*complex_byte_len + 2] << 16; //hi
            uint32_t cir_img_2 =
                cir_buffer_bulk[1 + i*complex_byte_len + 3] //lo
                | cir_buffer_bulk[1 + i*complex_byte_len + 4] << 8 //mid
                | cir_buffer_bulk[1 + i*complex_byte_len + 5] << 16; //hi

            float ciri2 = (float)convert_u24_to_i24(cir_img_2);
            float cirr2 = (float)convert_u24_to_i24(cir_real_2);

            float phase = atan2(ciri2, cirr2);

            float amplitude = sqrtf(ciri2 * ciri2 + cirr2 * cirr2);

            magnitude_bulk[i] = amplitude;
            phase_bulk[i] = phase;

        }
        for(size_t i = 0; i < debug_sample_size; ++i) {
            Serial.print(magnitude_bulk[i]);
            Serial.print(",");
        }
        Serial.println("");
        for(size_t i = 0; i < debug_sample_size; ++i) {
            Serial.print(phase_bulk[i]);
            Serial.print(",");
        }
        Serial.println("");
        Serial.println(fp_index);

        return;

    }

}
```
The result yielded the graph I attached.

Yet with these changes, I don't see the phases from T2 and T4 properly canceling their drift.





Using the second entry (FP + 1) should not affect things, as seen by the graph, the phase angle between both samples is about the same. I tested the code using just FP and it had no noticeable improvements.


Up to now, I've been using a serial plotter to see how the cir from each radio compares over time. That's how I can see that it's "random" without correlation, even before I add things together.

