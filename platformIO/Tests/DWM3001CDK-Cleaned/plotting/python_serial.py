import time
import random
import matplotlib.pyplot as plt
import serial
from matplotlib.figure import Figure
from matplotlib.axes import Axes
from collections import deque
import re
from itertools import batched
import math

####################################GRAPH STUFF

#turn on interactive mode
plt.ion()


#get plot sub-parts
figax = plt.subplots(3, 1)
fig: Figure = figax[0]
axes = figax[1]
graph_0: Axes = axes[0]
graph_1: Axes = axes[1]
graph_2: Axes = axes[2]


#create an initial empty line objects
#anchor is the device connected to the PC through serial, tag is the remote one
graph_0_ln0, = graph_0.plot([], [], color='r',marker='o')
graph_0_ln1, = graph_0.plot([], [], color='b',marker='o')

graph_1_ln0, = graph_1.plot([], [], color='r',marker='o')
graph_1_ln1, = graph_1.plot([], [], color='b',marker='o')


graph_2_ln1, = graph_2.plot([], [], color='r',  marker='o')
graph_2_ln2, = graph_2.plot([], [], color='b',  marker='o')
graph_2_ln0, = graph_2.plot([], [], color='g',  marker='o')
plot_ln_0: list[float] = []
plot_ln_1: list[float] = []
plot_ln_2: list[float] = []
plot_ln_3: list[float] = []

#pre-set limits if you know them, or auto-scale later
#magnitude_graph.set_xlim(0, 20)
graph_0.set_ylim(0, 6.3)
#phase_graph.set_xlim(0, 20)
graph_1.set_ylim(0, 6.3)

graph_2.set_ylim(0, 0.6)

#take new x and y data and put it on the graph
def update_plot_data(
        plot_item_0: float,
        plot_item_1: float,
        plot_item_2_0: float,
        plot_item_2_1: float,
        ):


    plot_ln_0.append(plot_item_0)
    plot_ln_1.append(plot_item_1)
    plot_ln_2.append(plot_item_2_0)
    plot_ln_3.append(plot_item_2_1)

    if(len(plot_ln_0) > 200):
        plot_ln_0.pop(0)
        plot_ln_1.pop(0)
        plot_ln_2.pop(0)
        plot_ln_3.pop(0)


    #create x values to go with the input y values
    canceled_x_vals: list[float] = []
    for i in range(len(plot_ln_0)):
        canceled_x_vals.append(i)

    graph_2_ln0.set_xdata(canceled_x_vals)
    graph_2_ln0.set_ydata(plot_ln_2)
    graph_2_ln1.set_xdata(canceled_x_vals)
    graph_2_ln1.set_ydata(plot_ln_3)
    #graph_2_ln2.set_xdata(canceled_x_vals)
    #graph_2_ln2.set_ydata(plot_ln_2)




    #update the data inside the line object directly
    graph_0_ln0.set_xdata(canceled_x_vals)
    graph_0_ln0.set_ydata(plot_ln_0)
    #graph_0_ln1.set_xdata(tag_mag_x)
    #graph_0_ln1.set_ydata(tag_mag_y)

    graph_1_ln0.set_xdata(canceled_x_vals)
    graph_1_ln0.set_ydata(plot_ln_1)
    #graph_1_ln1.set_xdata(tag_phase_x)
    #graph_1_ln1.set_ydata(tag_phase_y)

    
    #adjust axes limits dynamically if data exceeds views
    graph_0.relim()
    graph_0.autoscale_view()
    graph_1.relim()
    graph_1.autoscale_view()
    graph_2.relim()
    graph_2.autoscale_view()

    #force redraw and pause briefly to let the GUI refresh
    fig.canvas.draw()
    fig.canvas.flush_events()

    ...




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

moving_average_window_size = 6
moving_average_array: list[float] = []
def moving_average(new_entry: float) -> float:

    moving_average_array.append(new_entry)
    if(len(moving_average_array) > moving_average_window_size):
        moving_average_array.pop(0)
        ...

    average = 0    
    for i in moving_average_array:
        average += i
        ...

    average /= len(moving_average_array)
    return average

    ...



#####################################SERIAL STUFF


#data populating test
while False:

    xd: list[float] = []
    yd: list[float] = []
    yd2: list[float] = []

    for i in range(20):
        xd.append(i)
        yd.append(random.randint(0,10))
        yd2.append(random.randint(0,10))

    update_plot_data(xd, yd, #anchor real
                     xd, yd2, #tag real
                     xd, yd2, #anchor img
                     xd, yd #tag img
                     )
    
    time.sleep(0.1)


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


        #successfully opened, start the mainloop
        while True:

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
                        #cfo_fine_ch9 %= wrap_factor

                        slow_drift_offset_fine9 = 2*math.pi*(FREQ_CH9)*cfo_fine_ch9*1e-6*sample_delay
                        #print(f"slow_drift_offset_fine9: {slow_drift_offset_fine9}")


                        #########################################CFO_Fine with channel 5


                        wrap_factor = 1/(FREQ_CH5)/sample_delay*1e6
                        #phase_diff_ch5 = (ch5_final_phase - ch5_pfinal_phase)

                        ambiguity = math.floor(cfo_coarse / wrap_factor)
                        cfo_fine_ch5 = (phase_diff_ch5 / (2 * math.pi) + ambiguity) * wrap_factor
                        if(cfo_fine_ch5 - cfo_coarse < wrap_factor / 2):
                            cfo_fine_ch5 += wrap_factor
                        if(cfo_fine_ch5 - cfo_coarse > wrap_factor / 2):
                            cfo_fine_ch5 -= wrap_factor
                        #cfo_fine_ch5 %= wrap_factor

                        slow_drift_offset_fine5 = 2*math.pi*(FREQ_CH5)*cfo_fine_ch5*1e-6*sample_delay
                        #print(f"slow_drift_offset_fine5: {slow_drift_offset_fine5}")

                        #print(f"cfo_raw_average: {cfo_raw_average}")

                    





                        #phase wrap
                        if(ch5_poll_phase < 0):
                            ch5_poll_phase += 2 * math.pi
                        if(ch5_response_phase < 0):
                            ch5_response_phase += 2 * math.pi
                        if(ch5_final_phase < 0):
                            ch5_final_phase += 2 * math.pi


                        canceled_cir_1 = ch5_poll_phase + ch5_response_phase - slow_drift_offset_ch5 #found_phase_b_diff
                        canceled_cir_2 = ch9_poll_phase + ch9_response_phase - slow_drift_offset_fine9
                        canceled_cir_3 = ch5_poll_phase + ch5_response_phase - slow_drift_offset_fine5 #found_phase_b_diff
                        canceled_cir_av = (canceled_cir_2 + canceled_cir_3) / 2
                        canceled_cir_1 = canceled_cir_1 % (2 * math.pi)
                        canceled_cir_2 = canceled_cir_2 % (2 * math.pi)
                        canceled_cir_3 = canceled_cir_3 % (2 * math.pi)
                        canceled_cir_av = canceled_cir_av % (2 * math.pi)
                        
                        
                        #######################################Combining ch5 and ch9 phases to increase ambiguity cycle

                        virtual_phase = canceled_cir_2 - canceled_cir_3
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

                        distance_twr = tof_average * SPEED_OF_LIGHT * DWT_TIME_UNITS

                        #print(f"TOF Distance: {distance_twr}", end="\t")


                        #######################################ambiguity resolution using using rough distance for the ch5-ch9 virtual phase

                        virtual_wavelength = SPEED_OF_LIGHT / (2 * (FREQ_CH9 - FREQ_CH5))
                        virtual_ambiguity_ct = math.floor(distance_twr / virtual_wavelength)


                        print(f"Virtual N: {virtual_ambiguity_ct}", end="\t")

                        distance_virtual = (virtual_phase / (2 * math.pi) + virtual_ambiguity_ct) * virtual_wavelength                        
                        distance_virtual = moving_average(distance_virtual) #In theory, the device will be polled much faster, making this more viable
                        print(f"Virtual Dist: {distance_virtual}")


                        #######################################ambiguity resolution using the virtual phase
                        #(Todo)


                        


              

                        update_plot_data(
                                        #green, red, blue
                                        #drift_estimate_from_cfo, drift_estimate_from_clock, drift_estimate
                                        canceled_cir_2,canceled_cir_3,distance_virtual,distance_twr

                                        )





            fig.canvas.draw()
            fig.canvas.flush_events()
            time.sleep(0.01)
                



except serial.SerialException as e:
    print(f"Error opening or using serial port: {e}")



#keep the final plot open when the loop finishes
plt.ioff()
plt.show()
