import collections
import math
import time
import serial
import dearpygui.dearpygui as dpg
from itertools import batched
import math
from collections import deque
import re

#####################################HELPER FUNCTIONS

#collect a chunk of data from a queue holding bytes
def grab_data_chunk(queue: deque[int]) -> list[int]:

    #move to the starting point
    while(True):
        #ran out of incoming bytes, return. We hadn't hit the start delimiter, so don't put the bytes back
        if(len(queue) < 1):
            return []
        
        output = queue.popleft()

        if(output == ord('A')):
            queue.appendleft(output) #put it back
            break

    bytelist: list[int] = []
    while(True):
        #ran out before we hit the end-of-delimiter, put the bytes back
        if(len(queue) < 1):
            for i in range(len(bytelist) - 1, -1, -1):
                queue.appendleft(bytelist[i])
            return []


        output = queue.popleft()

        bytelist.append(output)

        if(output == ord('B')):
            break





    return bytelist


    ...

def parse_cir_line(data: str) -> tuple[list[float], list[float]]:

    numbers = re.split(r',', data)
    if(len(numbers) % 2 == 1):
        numbers.pop()

    phase_data = []
    mag_data = []
    for (phase, mag) in batched(numbers, 2):

        phase_data.append(float(phase))
        mag_data.append(float(mag))
        ...

    return (phase_data, mag_data)

def wrap_to_pi(phase_angle: float) -> float:
    return (phase_angle + math.pi) % (2 * math.pi) - math.pi
    ...


#does a simple moving average over a certain timespan
class MovingAverage:
    def __init__(self, window_size: int = 6):
        self.window_size = window_size
        self.array: list[float] = []


    def add(self, new_entry: float) -> float:
        self.array.append(new_entry)
        if(len(self.array) > self.window_size):
            self.array.pop(0)

        average = 0    
        for i in self.array:
            average += i

        average /= len(self.array)
        return average


class PhaseUnwrapper:

    def __init__(self):
        self.last_entry = 0

    def unwrap_entry(self, entry: float) -> float:
        #ensure entry is within 0->2pi
        entry = entry % (2*math.pi)

        #shrink down the unwrapped entry by the same amount
        last_entry = self.last_entry % (2 * math.pi)
        unwrapped_count = self.last_entry // (2 * math.pi)


        #if we jumped by more than half a phase, assume we wrapped
        if(abs(last_entry - entry) > (math.pi * 1.0)):
            ...
            jump_sign = (last_entry > entry) - (last_entry < entry)
            unwrapped_count += jump_sign

        self.last_entry = entry + (unwrapped_count * 2 * math.pi)
        return self.last_entry

    ...


#####################################GRAPH SETUP STUFF


#initialize fixed-length buffers for X and Y data
MAX_POINTS = 200
x_data = collections.deque(maxlen=MAX_POINTS)
y_data = collections.deque(maxlen=MAX_POINTS)

start_time = time.time()

dpg.create_context()

#create an imgui window and plots within it
with dpg.window(label="Rolling Line Graphs", width=800, height=820):
    with dpg.plot(label="Phase 0", height=-0.3, width=-1):
        dpg.add_plot_legend()
        
        #make x and y axis
        dpg.add_plot_axis(dpg.mvXAxis, label="Time (s)", tag="x_0_axis")
        with dpg.plot_axis(dpg.mvYAxis, label="Amplitude", tag="y_0_axis"):
            dpg.add_line_series([], [], label="Signal", tag="phase_one")
        #autoscale
        dpg.set_axis_limits("y_0_axis", 0, 2 * math.pi)

    with dpg.plot(label="Phase 1", height=-0.3, width=-1):
        dpg.add_plot_legend()
        
        #make x and y axis
        dpg.add_plot_axis(dpg.mvXAxis, label="Time (s)", tag="x_1_axis")
        with dpg.plot_axis(dpg.mvYAxis, label="Amplitude", tag="y_1_axis"):
            dpg.add_line_series([], [], label="Signal", tag="phase_two")
        #autoscale
        dpg.set_axis_limits("y_1_axis", 0, 2 * math.pi)

    with dpg.plot(label="Distance", height=-0.3, width=-1):
        dpg.add_plot_legend()
        
        #make x and y axis
        dpg.add_plot_axis(dpg.mvXAxis, label="Time (s)", tag="x_2_axis")
        with dpg.plot_axis(dpg.mvYAxis, label="Amplitude", tag="y_2_axis"):
            dpg.add_line_series([], [], label="Signal", tag="distances")
        #autoscale
        dpg.set_axis_limits("y_2_axis", 0, 0.3)



#open the window
dpg.create_viewport(title="UWB Plot", width=820, height=840)
dpg.setup_dearpygui()
dpg.show_viewport()

#take new x and y data and put it on the graph
x_val: float = 0.0
plot_x_vals: list[float] = []
plot_ln_0: list[float] = []
plot_ln_1: list[float] = []
plot_ln_2: list[float] = []
plot_ln_3: list[float] = []
def update_plot_data(
        plot_item_0: float,
        plot_item_1: float,
        plot_item_2_0: float,
        plot_item_2_1: float,
        ):
    global x_val

    plot_x_vals.append(x_val)
    x_val += 1.0

    plot_ln_0.append(plot_item_0)
    plot_ln_1.append(plot_item_1)
    plot_ln_2.append(plot_item_2_0)
    plot_ln_3.append(plot_item_2_1)

    if(len(plot_ln_0) > MAX_POINTS):
        plot_ln_0.pop(0)
        plot_ln_1.pop(0)
        plot_ln_2.pop(0)
        plot_ln_3.pop(0)


    #create x values to go with the input y values
    canceled_x_vals: list[float] = []
    for i in range(len(plot_ln_0)):
        canceled_x_vals.append(i)

    dpg.set_value("phase_one", [canceled_x_vals, plot_ln_0])
    dpg.set_value("phase_two", [canceled_x_vals, plot_ln_1])
    dpg.set_value("distances", [canceled_x_vals, plot_ln_2])



    #move graph along
    if len(canceled_x_vals) > 1:
        dpg.set_axis_limits("x_0_axis", canceled_x_vals[0], canceled_x_vals[-1])
        dpg.set_axis_limits("x_1_axis", canceled_x_vals[0], canceled_x_vals[-1])
        dpg.set_axis_limits("x_2_axis", canceled_x_vals[0], canceled_x_vals[-1])
    ...

#####################################SERIAL STUFF (main)

com_port_name = "COM17"
try:
    #open the port with baud 460800
    with serial.Serial(com_port_name, 460800, timeout=1) as ser:
        print(f"Opened port: {ser.name}")
        
        #write data example: must be prefixed with b for bytes
        #ser.write(b'Write\n')        
        #read data example
        #response = ser.readline()
        #print(f"Received: {response.decode('utf-8', errors='ignore')}")

        queue: deque[int] = deque()

        moving_av_1: MovingAverage = MovingAverage(6)
        moving_av_2: MovingAverage = MovingAverage(6)
        moving_av_slow5: MovingAverage = MovingAverage(6)
        moving_av_slow9: MovingAverage = MovingAverage(6)
        phase_unwrapper: PhaseUnwrapper = PhaseUnwrapper()


        #successfully opened, start the mainloop
        #main loop
        while dpg.is_dearpygui_running():


            #read in and parse serial data
            last_canceled_cir: float = 0

            bytes_ready = ser.in_waiting
            if(bytes_ready > 0):

                incoming_bytes = ser.read(bytes_ready)

                for i in range(len(incoming_bytes)):
                    queue.append(incoming_bytes[i])


                #try to process all the new data we can
                while(True):
                    chunk = grab_data_chunk(queue)
                    if len(chunk) == 0:
                        break
                    else:
                        #print(len(chunk))
                        ascii_string = bytes(chunk).decode("ascii")

                        #split into rows
                        parts = re.split(r'\n', ascii_string)

                        #split into cols (first column is 'A')
                        numbers_ch5 = re.split(r',', parts[1])
                        numbers_ch9 = re.split(r',', parts[2])
                        


                        if(len(numbers_ch5) < 8 or len(numbers_ch9) < 8):
                            continue

                        ########################CH5
                        #phases of each communication burst
                        ch5_poll_phase = float(numbers_ch5[0])
                        ch5_response_phase = float(numbers_ch5[1])
                        ch5_final_phase = float(numbers_ch5[2])
                        ch5_pfinal_phase = float(numbers_ch5[3])

                        #carrier integrators for doing part of drift cancellation, though we really only need one of these
                        ch5_final_ci = float(numbers_ch5[4])
                        ch5_pfinal_ci = float(numbers_ch5[5])

                        #use these for basic DSTWR calculations
                        ch5_round_1_time = float(numbers_ch5[6])
                        ch5_reply_1_time = float(numbers_ch5[7])
                        ch5_round_2_time = float(numbers_ch5[8])
                        ch5_reply_2_time = float(numbers_ch5[9])

                        ########################CH9
                        #phases of each communication burst
                        ch9_poll_phase = float(numbers_ch9[0])
                        ch9_response_phase = float(numbers_ch9[1])
                        ch9_final_phase = float(numbers_ch9[2])
                        ch9_pfinal_phase = float(numbers_ch9[3])

                        #carrier integrators for doing part of drift cancellation, though we really only need one of these
                        ch9_final_ci = float(numbers_ch9[4])
                        ch9_pfinal_ci = float(numbers_ch9[5])

                        #use these for basic DSTWR calculations
                        ch9_round_1_time = float(numbers_ch9[6])
                        ch9_reply_1_time = float(numbers_ch9[7])
                        ch9_round_2_time = float(numbers_ch9[8])
                        ch9_reply_2_time = float(numbers_ch9[9])




                        #constants pulled from the qorvo API
                        SPEED_OF_LIGHT = 299702547
                        FREQ_OFFSET_MULTIPLIER = (998.4e6 / 2.0 / 1024.0 / 131072.0)
                        FREQ_CH5 = 6489.6e6
                        FREQ_CH9 = 7987.2e6
                        WAVELENGTH_CH5 = SPEED_OF_LIGHT / FREQ_CH5
                        WAVELENGTH_CH9 = SPEED_OF_LIGHT / FREQ_CH9

                        HERTZ_TO_PPM_MULTIPLIER_CHAN_5 = (-1.0e6 / 6489.6e6)
                        HERTZ_TO_PPM_MULTIPLIER_CHAN_9 = (-1.0e6 / 7987.2e6)
                        DWT_TIME_UNITS = (1.0 / 499.2e6 / 128.0) #15.65e-12s per tick of the radio


                        #new experiment: residue cancellation using phase values

                        #represents cfo_coarse in the matlab example
                        ch5_cfo_coarse = FREQ_OFFSET_MULTIPLIER * ch5_final_ci * HERTZ_TO_PPM_MULTIPLIER_CHAN_5

                        #the time between the two rx samples at the receiver
                        #sample_delay = (round_1_time + reply_2_time) * DWT_TIME_UNITS
                        sample_delay = 4e-3

                        wrap_factor = 1/(FREQ_CH5)/sample_delay*1e6

                        #determine ambiguity
                        ambiguity = math.floor(ch5_cfo_coarse / wrap_factor)
                        #determine drift

                        #drift_estimate = ((ch5_poll_phase - ch5_final_phase) / (2 * math.pi) + ambiguity) * wrap_factor
                        drift_estimate = ((ch5_final_phase - ch5_pfinal_phase) / (2 * math.pi) + ambiguity) * wrap_factor
                        #drift_estimate %= wrap_factor

                        #when using poll and final instead of final and post-final, this is needed... I don't know why.
                        #we also get pi phase shifting with it. Also don't know why.
                        #drift_estimate /= 2

                        slow_drift_offset_ch5 = 2*math.pi*FREQ_CH5*drift_estimate*1e-6*sample_delay

                        #print(f"slow_drift_offset_ch5: {slow_drift_offset_ch5}", end="\t")

                        ########################################CFO_Coarse using both channels (a test)
                        #sample_delay = 8e-3

                        ch9_cfo_raw = FREQ_OFFSET_MULTIPLIER * ch9_final_ci * HERTZ_TO_PPM_MULTIPLIER_CHAN_9
                        ch5_cfo_raw = FREQ_OFFSET_MULTIPLIER * ch5_final_ci * HERTZ_TO_PPM_MULTIPLIER_CHAN_5

                        cfo_raw_average = (ch9_cfo_raw + ch5_cfo_raw) / 2 #not sure if we want to use the average for this, but we can...
                        wrap_factor = 1/(FREQ_CH9-FREQ_CH5)/sample_delay*1e6

                        phase_diff_ch5 = (ch5_final_phase - ch5_pfinal_phase)
                        phase_diff_ch9 = (ch9_final_phase - ch9_pfinal_phase)
                        phase_diff = phase_diff_ch9 - phase_diff_ch5

                        ambiguity = math.floor(cfo_raw_average / wrap_factor)

                        cfo_coarse = (phase_diff / (2 * math.pi) + ambiguity) * wrap_factor
                        if(cfo_coarse - cfo_raw_average < wrap_factor / 2):
                            cfo_coarse += wrap_factor
                        if(cfo_coarse - cfo_raw_average > wrap_factor / 2):
                            cfo_coarse -= wrap_factor                        
                        #cfo_coarse %= wrap_factor

                        slow_drift_offset_coarse = 2*math.pi*(FREQ_CH9-FREQ_CH5)*cfo_coarse*1e-6*sample_delay
                        #print(f"slow_drift_offset_coarse: {slow_drift_offset_coarse}", end="\t")

                        #########################################CFO_Fine with channel 9

                        wrap_factor = 1/(FREQ_CH9)/sample_delay*1e6
                        #phase_diff_ch9 = (ch9_final_phase - ch9_pfinal_phase)

                        ambiguity = math.floor(cfo_coarse / wrap_factor)
                        cfo_fine_ch9 = (phase_diff_ch9 / (2 * math.pi) + ambiguity) * wrap_factor
                        if(cfo_fine_ch9 - cfo_coarse < wrap_factor / 2):
                            cfo_fine_ch9 += wrap_factor
                        if(cfo_fine_ch9 - cfo_coarse > wrap_factor / 2):
                            cfo_fine_ch9 -= wrap_factor


                        #########################################CFO_Fine with channel 5


                        wrap_factor = 1/(FREQ_CH5)/sample_delay*1e6
                        #phase_diff_ch5 = (ch5_final_phase - ch5_pfinal_phase)

                        ambiguity = math.floor(cfo_coarse / wrap_factor)
                        cfo_fine_ch5 = (phase_diff_ch5 / (2 * math.pi) + ambiguity) * wrap_factor
                        if(cfo_fine_ch5 - cfo_coarse < wrap_factor / 2):
                            cfo_fine_ch5 += wrap_factor
                        if(cfo_fine_ch5 - cfo_coarse > wrap_factor / 2):
                            cfo_fine_ch5 -= wrap_factor


                        ##############Average them

                        #if I try to use this, I get random jumps in the (mostly correct) data, so I'll avoid using this for now
                        cfo_fine_average = (cfo_fine_ch5 + cfo_fine_ch9) * 0.5


                        ###############turn the accurate clock offsets into phase offsets for each frequency
                        slow_drift_offset_fine5 = 2*math.pi*(FREQ_CH5)*cfo_fine_ch5*1e-6*sample_delay
                        slow_drift_offset_fine9 = 2*math.pi*(FREQ_CH9)*cfo_fine_ch9*1e-6*sample_delay
                        #print(f"slow_drift_offset_fine9: {slow_drift_offset_fine9}")

                        #smooth them out like what the research paper outlines
                        #slow_drift_offset_fine5 = moving_av_slow5.add(slow_drift_offset_fine5)
                        #slow_drift_offset_fine9 = moving_av_slow5.add(slow_drift_offset_fine9)

                    





                        #phase wrap
                        if(ch5_poll_phase < 0):
                            ch5_poll_phase += 2 * math.pi
                        if(ch5_response_phase < 0):
                            ch5_response_phase += 2 * math.pi
                        if(ch5_final_phase < 0):
                            ch5_final_phase += 2 * math.pi


                        #canceled_cir_1 = ch5_poll_phase + ch5_response_phase - slow_drift_offset_ch5 #found_phase_b_diff
                        ch9_recovered_phase = ch9_poll_phase + ch9_response_phase - slow_drift_offset_fine9
                        ch5_recovered_phase = ch5_poll_phase + ch5_response_phase - slow_drift_offset_fine5 #found_phase_b_diff
                        #canceled_cir_1 = canceled_cir_1 % (2 * math.pi)
                        ch9_recovered_phase = ch9_recovered_phase % (2 * math.pi)
                        ch5_recovered_phase = ch5_recovered_phase % (2 * math.pi)

                        #ch9_recovered_phase = moving_av_1.add(ch9_recovered_phase)
                        #ch5_recovered_phase = moving_av_2.add(ch5_recovered_phase)
                        
                        
                        #######################################Combining ch5 and ch9 phases to increase ambiguity cycle

                        virtual_phase = ch5_recovered_phase - ch9_recovered_phase
                        virtual_phase = virtual_phase % (2 * math.pi)


                        #######################################normal DSTWR to get rough distance

                        ch5_tof = (
                            (ch5_round_1_time * ch5_round_2_time - ch5_reply_1_time * ch5_reply_2_time) / 
                            (ch5_round_1_time + ch5_reply_1_time + ch5_round_2_time + ch5_reply_2_time)
                            )

                        ch9_tof = (
                            (ch9_round_1_time * ch9_round_2_time - ch9_reply_1_time * ch9_reply_2_time) / 
                            (ch9_round_1_time + ch9_reply_1_time + ch9_round_2_time + ch9_reply_2_time)
                            )
                        
                        tof_average = (ch5_tof + ch9_tof) / 2
                        
                        #engineering


                        distance_twr = tof_average * SPEED_OF_LIGHT * DWT_TIME_UNITS

                        #distance_twr = moving_average(distance_twr)

                        distance_twr = moving_av_1.add(distance_twr)

                        print(f"Distance TWR: {distance_twr}", end="\t")


                        #######################################ambiguity resolution using using rough distance for the ch5-ch9 virtual phase

                        #both of these equations yield the same thing
                        virtual_wavelength = SPEED_OF_LIGHT / (2*(FREQ_CH9 - FREQ_CH5))
                        #virtual_wavelength = (WAVELENGTH_CH5 * WAVELENGTH_CH9) / (WAVELENGTH_CH5 - WAVELENGTH_CH9)# * 0.5
                        
                        virtual_ambiguity_ct = round((distance_twr - virtual_wavelength) / virtual_wavelength)

                        #virtual_phase += math.pi
                        #virtual_phase %= (2*math.pi)

                        distance_virtual = (virtual_phase / (2 * math.pi) + virtual_ambiguity_ct) * virtual_wavelength

                        #distance_virtual = (phase_unwrapper.unwrap_entry(virtual_phase) / (2*math.pi)) * virtual_wavelength
                        
                        #looping protection
                        if(distance_virtual - distance_twr < -(virtual_wavelength * 0.5)):
                           distance_virtual += virtual_wavelength
                        elif(distance_virtual - distance_twr > (virtual_wavelength * 0.5)):
                           distance_virtual -= virtual_wavelength

                        #distance_virtual = (-virtual_phase / 2 / math.pi + virtual_ambiguity_ct)*virtual_wavelength

                        #distance_virtual = moving_average(distance_virtual) #In theory, the device will be polled much faster, making this more viable
                        print(f"Virtual Dist: {distance_virtual}")


                        #######################################ambiguity resolution using the virtual phase
                        #(Todo)

                        #resolve for 5
                        ch5_ambiguity_ct = math.floor(distance_virtual / WAVELENGTH_CH5)
                        distance_ch5 = (ch5_recovered_phase / (2 * math.pi) + ch5_ambiguity_ct) * WAVELENGTH_CH5
                        #if(distance_ch5 - distance_virtual < (WAVELENGTH_CH5 * 0.5)):
                        #    distance_ch5 += WAVELENGTH_CH5
                        #elif(distance_ch5 - distance_virtual > (WAVELENGTH_CH5 * 0.5)):
                        #    distance_ch5 -= WAVELENGTH_CH5


                        #resolve for 9
                        ch9_ambiguity_ct = math.floor(distance_virtual / WAVELENGTH_CH9)
                        distance_ch9 = (ch9_recovered_phase / (2 * math.pi) + ch9_ambiguity_ct) * WAVELENGTH_CH9
                        if(distance_ch9 - distance_virtual < (WAVELENGTH_CH9 * 0.5)):
                            distance_ch9 += WAVELENGTH_CH9
                        elif(distance_ch9 - distance_virtual > (WAVELENGTH_CH9 * 0.5)):
                            distance_ch9 -= WAVELENGTH_CH9

                        dist_average = (distance_ch5 + distance_ch9) / 2


                        #print(f"Dist5: {distance_ch5}\t\t Dist9: {distance_ch9}\t\t DistA: {dist_average}")

                        


              
                        if(True):
                            update_plot_data(
                                        #green, red, blue
                                        #green, red
                                        #drift_estimate_from_cfo, drift_estimate_from_clock, drift_estimate
                                        #canceled_cir_2,canceled_cir_3,distance_virtual,distance_twr
                                        ch9_recovered_phase,virtual_phase,distance_virtual,distance_twr

                                        #ch5_recovered_phase,virtual_phase,dist_average,distance_twr

                                        )



















            # Calculate elapsed time and new data value
            t = time.time() - start_time
            val = math.sin(t * 3.0) + 0.2 * math.cos(t * 15.0)
            
            # Append to deque (automatically drops oldest item when full)
            x_data.append(t)
            y_data.append(val)
            
            # Update the plot series (must convert deque to list)
            #dpg.set_value("series_tag", [list(x_data), list(y_data)])
            
            # Scroll the X-axis to match the current data window
            #if len(x_data) > 1:
            #    dpg.set_axis_limits("x_axis", x_data[0], x_data[-1])
                
            dpg.render_dearpygui_frame()





except serial.SerialException as e:
    print(f"Error opening or using serial port: {e}")






#cleanup
dpg.destroy_context()

