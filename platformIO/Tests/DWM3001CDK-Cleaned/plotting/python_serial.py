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
magnitude_graph: Axes = axes[0]
phase_graph: Axes = axes[1]
canceled_graph: Axes = axes[2]


#create an initial empty line objects
#anchor is the device connected to the PC through serial, tag is the remote one
anchor_mag_ln, = magnitude_graph.plot([], [], color='r',)
tag_mag_ln, = magnitude_graph.plot([], [], color='b',)

anchor_phase_ln, = phase_graph.plot([], [], color='r',)
tag_phase_ln, = phase_graph.plot([], [], color='b',)


canceled_phase2_ln, = canceled_graph.plot([], [], color='r',  marker='o')
canceled_phase3_ln, = canceled_graph.plot([], [], color='b',  marker='o')
canceled_phase_ln, = canceled_graph.plot([], [], color='g',  marker='o')
canceled_y_array: list[float] = []
phase_anchor_array: list[float] = []
phase_tag_array: list[float] = []

#pre-set limits if you know them, or auto-scale later
#magnitude_graph.set_xlim(0, 20)
magnitude_graph.set_ylim(0, 3500)
#phase_graph.set_xlim(0, 20)
phase_graph.set_ylim(0, 6.3)

display_phase_graph = False
if display_phase_graph:
    canceled_graph.set_ylim(-2, 2)
    canceled_graph.set_xlim(-2, 2)
else:
    canceled_graph.set_ylim(0, 6.3)

#take new x and y data and put it on the graph
def update_plot_data(
        anchor_mag_x: list[float], anchor_mag_y: list[float],
        tag_mag_x: list[float], tag_mag_y: list[float],
        anchor_phase_x: list[float], anchor_phase_y: list[float],
        tag_phase_x: list[float], tag_phase_y: list[float],
        canceled_y: float,
        canceled_y2: float,
        canceled_y3: float,
        ):

    #ensure inputs are equal in length
    if(len(anchor_mag_x) != len(anchor_mag_y) or
       len(tag_mag_x) != len(tag_mag_y) or
       len(anchor_phase_x) != len(anchor_phase_y) or
       len(tag_phase_x) != len(tag_phase_y)
       ):
        return

    canceled_y_array.append(canceled_y)
    phase_anchor_array.append(canceled_y2)
    phase_tag_array.append(canceled_y3)

    if(len(canceled_y_array) > 200):
        canceled_y_array.pop(0)
        phase_anchor_array.pop(0)
        phase_tag_array.pop(0)

    canceled_x_vals: list[float] = []
    for i in range(len(canceled_y_array)):
        canceled_x_vals.append(i)

    canceled_phase_ln.set_xdata(canceled_x_vals)
    canceled_phase_ln.set_ydata(canceled_y_array)

    canceled_phase2_ln.set_xdata(canceled_x_vals)
    canceled_phase2_ln.set_ydata(phase_anchor_array)
    canceled_phase3_ln.set_xdata(canceled_x_vals)
    canceled_phase3_ln.set_ydata(phase_tag_array)

    #test: circle
    if display_phase_graph:
        x_val = math.cos(canceled_y)
        y_val = math.sin(canceled_y)

        canceled_phase_ln.set_xdata([0, x_val])
        canceled_phase_ln.set_ydata([0, y_val])

        x_val2 = math.cos(canceled_y + math.pi)
        y_val2 = math.sin(canceled_y + math.pi)

        x_val3 = math.cos(canceled_y2)
        y_val3 = math.sin(canceled_y2)
        x_val3_neg = math.cos(canceled_y2 + math.pi)
        y_val3_neg = math.sin(canceled_y2 + math.pi)

        canceled_phase2_ln.set_xdata([0, x_val2])
        canceled_phase2_ln.set_ydata([0, y_val2])
        #canceled_phase3_ln.set_xdata([x_val3_neg, 0, x_val3])
        #canceled_phase3_ln.set_ydata([y_val3_neg, 0, y_val3])



    #update the data inside the line object directly
    anchor_mag_ln.set_xdata(anchor_mag_x)
    anchor_mag_ln.set_ydata(anchor_mag_y)
    tag_mag_ln.set_xdata(tag_mag_x)
    tag_mag_ln.set_ydata(tag_mag_y)

    anchor_phase_ln.set_xdata(anchor_phase_x)
    anchor_phase_ln.set_ydata(anchor_phase_y)
    tag_phase_ln.set_xdata(tag_phase_x)
    tag_phase_ln.set_ydata(tag_phase_y)

    
    #adjust axes limits dynamically if data exceeds views
    magnitude_graph.relim()
    magnitude_graph.autoscale_view()
    phase_graph.relim()
    phase_graph.autoscale_view()
    canceled_graph.relim()
    canceled_graph.autoscale_view()

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

moving_average_window_size = 4
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
                        FREQ_OFFSET_MULTIPLIER = (998.4e6 / 2.0 / 1024.0 / 131072.0)
                        FREQ_CH5 = 6489.6e6
                        FREQ_CH9 = 7987.2e6
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

                        cfo_fine_av = (cfo_fine_ch5 + cfo_fine_ch9) / 2
                        cfo_fine_av %= (2 * math.pi)





                        #phase wrap
                        if(ch5_poll_phase < 0):
                            ch5_poll_phase += 2 * math.pi
                        if(ch5_response_phase < 0):
                            ch5_response_phase += 2 * math.pi
                        if(ch5_final_phase < 0):
                            ch5_final_phase += 2 * math.pi


                        canceled_cir_1 = ch5_poll_phase + ch5_response_phase - slow_drift_offset_ch5 #found_phase_b_diff
                        canceled_cir_2 = ch5_poll_phase + ch5_response_phase - slow_drift_offset_fine9
                        canceled_cir_3 = ch5_poll_phase + ch5_response_phase - slow_drift_offset_fine5 #found_phase_b_diff
                        canceled_cir_1 = canceled_cir_1 % (2 * math.pi)
                        canceled_cir_2 = canceled_cir_2 % (2 * math.pi)
                        canceled_cir_3 = canceled_cir_3 % (2 * math.pi)



              

                        update_plot_data([0], [0], #anchor mag
                                        [0], [0], #tag mag
                                        [0], [0], #anchor phase
                                        [0], [0], #tag phase
                                        #green, red, blue
                                        #drift_estimate_from_cfo, drift_estimate_from_clock, drift_estimate
                                        canceled_cir_3,0,canceled_cir_1
                                        )





            fig.canvas.draw()
            fig.canvas.flush_events()
            time.sleep(0.01)
                



except serial.SerialException as e:
    print(f"Error opening or using serial port: {e}")



#keep the final plot open when the loop finishes
plt.ioff()
plt.show()
