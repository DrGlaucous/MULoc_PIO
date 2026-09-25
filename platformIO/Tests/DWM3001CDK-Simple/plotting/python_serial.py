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


com_port_name = "COM11"
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


                        parts = re.split(r'\n', ascii_string)
                        poll_cir = ([],[])
                        response_cir = ([],[])
                        final_cir = ([],[])
                        post_final_cir = ([],[])
                        post_post_final_cir = ([],[])
                        p3f_cir = ([],[])

                        try:
                            poll_cir = parse_cir_line(parts[1])
                            response_cir = parse_cir_line(parts[2])
                            final_cir = parse_cir_line(parts[3])
                            post_final_cir = parse_cir_line(parts[4])
                            post_post_final_cir = parse_cir_line(parts[5])
                            p3f_cir = parse_cir_line(parts[6])
                        except:
                            break




                        # #this method is not supposed to handle this, but we're doing it anyways
                        carrier_integrators = parse_cir_line(parts[7])
                        poll_ci = carrier_integrators[0][0]
                        final_ci = carrier_integrators[1][0]
                        post_final_ci = carrier_integrators[0][1]
                        post_post_final_ci = carrier_integrators[1][1]
                        remote_held_time = carrier_integrators[0][2]
                        local_held_time = carrier_integrators[1][2]


                        #constants pulled from the qorvo API
                        FREQ_OFFSET_MULTIPLIER = (998.4e6 / 2.0 / 1024.0 / 131072.0)
                        HERTZ_TO_PPM_MULTIPLIER_CHAN_5 = (-1.0e6 / 6489.6e6)

                        #equivalent units to the cfo variable in the muloc code (may need to be multiplied by -1)
                        poll_freq_offset_hz = FREQ_OFFSET_MULTIPLIER * poll_ci
                        final_freq_offset_hz = FREQ_OFFSET_MULTIPLIER * final_ci
                        post_final_freq_offset_hz = FREQ_OFFSET_MULTIPLIER * post_final_ci
                        post_post_final_freq_offset_hz = FREQ_OFFSET_MULTIPLIER * post_post_final_ci

                        poll_rotations_per_sample = 2 * math.pi * poll_freq_offset_hz * (1. / 499.22e6)
                        final_rotations_per_sample = 2 * math.pi * final_freq_offset_hz * (1. / 499.22e6)
                        post_final_rotations_per_sample = 2 * math.pi * post_final_freq_offset_hz * (1. / 499.22e6)
                        post_post_final_rotations_per_sample = 2 * math.pi * post_post_final_freq_offset_hz * (1. / 499.22e6)

                        drift_estimate_from_cfo = (post_final_freq_offset_hz * HERTZ_TO_PPM_MULTIPLIER_CHAN_5)
                        #foffset = 2*math.pi*post_final_freq_offset_hz*8000e-6
                        #foffset = 2*math.pi*6489.6e6*drift_estimate_from_cfo*1e-6*8000e-6
                        foffset = -2*math.pi*post_final_freq_offset_hz*8000e-6

                        
                        clock_offset = (remote_held_time - local_held_time) / local_held_time                        
                        freq_offset = 6489.6e6 * clock_offset
                        drift_estimate_from_clock = freq_offset * HERTZ_TO_PPM_MULTIPLIER_CHAN_5
                        foffset2 = -2*math.pi*freq_offset*8000e-6
                        #foffset2 = 2*math.pi*6489.6e6*drift_estimate_from_clock*1e-6*8000e-6





                        #new experiment: residue cancellation using phase values
                        wrap_factor = 1/(6489.6e6)/8e-3*1e6

                        #determine ambiguity
                        ambiguity = math.floor((final_freq_offset_hz * HERTZ_TO_PPM_MULTIPLIER_CHAN_5) / wrap_factor)
                        #determine drift

                        drift_estimate = ((final_cir[0][9] - post_final_cir[0][9]) / (2 * math.pi) + ambiguity) * wrap_factor

                        foffset3 = 2*math.pi*6489.6e6*drift_estimate*1e-6*8000e-6

                        print(f"Foffset: {foffset:.2f}\t|| Foffset2: {foffset2:.4f}\t|| foffset3: {foffset3}")


                        # #read in carrier integrators and first path index values
                        # anchor_ci = carrier_integrators[0][0]
                        # tag_ci = carrier_integrators[1][0]
                        # anchor2_ci = carrier_integrators[0][1]
                        # #fp_index - 8 is where we start reading our samples
                        # tag_fp_index = carrier_integrators[1][1] - 8
                        # anchor_fp_index = carrier_integrators[0][2] - 8
                        # anchor2_fp_index = carrier_integrators[1][2] - 8
                        # #phase of arrival
                        # ip_poa_anchor = float(carrier_integrators[0][3]) / float(1 << 11)
                        # ip_poa_tag = float(carrier_integrators[1][3]) / float(1 << 11)
                        # #calculate rotations per sample for de-rotating them.
                        # #frequency = 6489.6e6
                        # tag_freq_offset_hz = FREQ_OFFSET_MULTIPLIER * tag_ci
                        # anchor_freq_offset_hz = FREQ_OFFSET_MULTIPLIER * anchor_ci
                        # anchor2_freq_offset_hz = FREQ_OFFSET_MULTIPLIER * anchor2_ci
                        # #offset_ratio = tag_second_ci * FREQ_OFFSET_MULTIPLIER * HERTZ_TO_PPM_MULTIPLIER_CHAN_5# / 1e6
                        # tag_rotation_per_sample = 2 * math.pi * tag_freq_offset_hz * (1. / 499.22e6)
                        # anchor_rotation_per_sample = 2 * math.pi * anchor_freq_offset_hz * (1. / 499.22e6)
                        # anchor2_rotation_per_sample = 2 * math.pi * anchor2_freq_offset_hz * (1. / 499.22e6)


                        #phase wrap and de-rotate
                        for i in range(len(poll_cir[0])):

                            #anchor_cir[0][i] = anchor_cir[0][i] - ip_poa_anchor 
                            #tag_cir[0][i] = tag_cir[0][i] - ip_poa_tag
                            #anchor_cir[0][i] = anchor_cir[0][i] - (anchor_fp_index + i) * anchor_rotation_per_sample
                            #tag_cir[0][i] = tag_cir[0][i] - (tag_fp_index + i) * tag_rotation_per_sample
                            #post_final_cir[0][i] = post_final_cir[0][i] - (anchor2_fp_index + i) * anchor2_rotation_per_sample


                            if(poll_cir[0][i] < 0):
                                poll_cir[0][i] += 2 * math.pi
                            if(response_cir[0][i] < 0):
                                response_cir[0][i] += 2 * math.pi
                            if(final_cir[0][i] < 0):
                                final_cir[0][i] += 2 * math.pi
                            if(post_final_cir[0][i] < 0):
                                post_final_cir[0][i] += 2 * math.pi
                            if(post_post_final_cir[0][i] < 0):
                                post_post_final_cir[0][i] += 2 * math.pi


                        #unfortunately, this doesn't have the accuracy needed to reliably cancel our residual
                        phase_a = final_cir[0][9]
                        phase_b = post_final_cir[0][9] #ground truth
                        phase_c = post_post_final_cir[0][9]
                        phase_d = p3f_cir[0][9]

                        a_b_time = 8000
                        a_c_time = 19000
                        c_d_time = 10200
                        a_d_time = a_c_time + c_d_time

                        b_c_time = a_c_time - a_b_time
                        b_d_time = a_d_time - a_b_time

                        a_c_percent = a_b_time / b_c_time
                        a_d_percent = a_b_time / b_d_time

                        #b_1 = (phase_a + a_c_percent * phase_c) / 1#(1 + a_c_percent)
                        #b_2 = (phase_a - a_c_percent * phase_c) / 1#(1 - a_c_percent)

                        #mathematical shortcut for a phase time of 0.5. Need to generalize it over any ratio
                        b_1 = (phase_a - phase_c) * (a_b_time / a_c_time)
                        b_2 = b_1 + math.pi
                        b_1 = b_1 % (2 * math.pi)
                        b_2 = b_2 % (2 * math.pi)

                        #either b1 or b2 will be the correct answer at this point, but new need to figure out which one to pick using only phase_d
                        b_3 = (phase_a - phase_d) * (a_b_time / a_d_time)
                        b_4 = b_3 + math.pi
                        b_3 = b_3 % (2 * math.pi)
                        b_4 = b_4 % (2 * math.pi)

                        b_5 = (phase_c - phase_d) * (a_b_time / c_d_time)
                        b_6 = b_5 + math.pi
                        b_5 = b_5 % (2 * math.pi)
                        b_6 = b_6 % (2 * math.pi)

                        #of the four points calculated above, find the two that are closest together (those should be the correct offset)
                        def shortest_angular_distance(angle_1: float, angle_2: float) -> float:
                            return abs((angle_1 - angle_2 + math.pi) % (2 * math.pi) - math.pi)

                        sha_dis_1 = shortest_angular_distance(b_1, b_3)
                        sha_dis_2 = a_d_percent * shortest_angular_distance(b_1, b_4)

                        sha_dis_3 = shortest_angular_distance(b_2, b_3)
                        sha_dis_4 = a_d_percent * shortest_angular_distance(b_2, b_4)

                        sha_dis_5 = a_d_percent * shortest_angular_distance(b_2, b_4)
                        sha_dis_6 = a_d_percent * shortest_angular_distance(b_2, b_4)

                        sha_dis_list = [sha_dis_1, sha_dis_2, sha_dis_3, sha_dis_4]
                        shortest_dist = min(sha_dis_list)

                        #draw a conclusion from the point we found
                        found_phase_b_diff = 0.0
                        if(shortest_dist == sha_dis_1):
                            found_phase_b_diff = (b_1 + b_3) * 0.5
                            ...
                        elif(shortest_dist == sha_dis_2):
                            found_phase_b_diff = (b_1 + b_4) * 0.5
                            ...
                        elif(shortest_dist == sha_dis_3):
                            found_phase_b_diff = (b_2 + b_3) * 0.5
                            ...
                        else:
                            found_phase_b_diff = (b_2 + b_4) * 0.5
                            ...

                        #difference between these two values is around 0 or around 2pi (0)
                        subs1 = (phase_a - phase_b) % (2 * math.pi)
                        subs2 = (phase_b - phase_c) % (2 * math.pi)

                        subsbig = b_1 #(phase_a - phase_c) % (2 * math.pi)

                        phase_cancellation_diff = (subs1 - subs2)
                        #print(f"Subs1: {subs1:.2f}\t|| subs2: {subs2:.4f}\t|| subsbig: {subsbig:.4f}")

                        #subs2 = 0







                        #I think I can use collect to make this go faster... oh, well.
                        x_vals: list[float] = []
                        for i in range(len(poll_cir[0])):
                            x_vals.append(float(i))

                        cancled_cir_1 = poll_cir[0][9] + response_cir[0][9] - foffset #(final_cir[0][9] - post_final_cir[0][9])
                        cancled_cir_2 = poll_cir[0][9] + response_cir[0][9] - foffset2 #(post_final_cir[0][9] - post_post_final_cir[0][9])
                        cancled_cir_3 = poll_cir[0][9] + response_cir[0][9] - foffset3 #found_phase_b_diff


                        #cancled_cir2 = anchor_cir[0][9] - tag_cir[0][9] + post_final_cir[0][9]
                        #cancled_cir2 = cancled_cir2 % (2 * math.pi)

                        #the version in the paper varies by about 0.6 radians

                        cancled_cir_1 = cancled_cir_1 % (2 * math.pi)
                        cancled_cir_2 = cancled_cir_2 % (2 * math.pi)
                        cancled_cir_3 = cancled_cir_3 % (2 * math.pi)

                        #cancled_cir = moving_average(cancled_cir)

                        #last_inverted_cir = (last_canceled_cir + math.pi) % (2 * math.pi)
                        #last_canceled_cir = cancled_cir

                        #submitted_cir = cancled_cir
                        #if(abs(cancled_cir - last_inverted_cir) < 0.6):
                        #    submitted_cir = (cancled_cir + math.pi) % (2 * math.pi)
                        



                        

                        #test: offset by 1/2 a phase if this happens
                        #if(delta_cir > math.pi * 0.5 and delta_cir < math.pi * 0.5):
                        #    cancled_cir += math.pi
                        #    cancled_cir = cancled_cir % (2 * math.pi)



                        

                        update_plot_data(x_vals, poll_cir[1], #anchor mag
                                        x_vals, response_cir[1], #tag mag
                                        x_vals, poll_cir[0], #anchor phase
                                        x_vals, response_cir[0], #tag phase
                                        #green, red, blue
                                        #drift_estimate_from_cfo, drift_estimate_from_clock, drift_estimate
                                        cancled_cir_1, cancled_cir_2, cancled_cir_3
                                        )





            fig.canvas.draw()
            fig.canvas.flush_events()
            time.sleep(0.01)
                



except serial.SerialException as e:
    print(f"Error opening or using serial port: {e}")



#keep the final plot open when the loop finishes
plt.ioff()
plt.show()
