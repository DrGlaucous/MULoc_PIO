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

                        #split into rows
                        parts = re.split(r'\n', ascii_string)

                        #split into cols (first column is 'A')
                        numbers = re.split(r',', parts[1])

                        if(len(numbers) < 8):
                            continue

                        poll_phase = float(numbers[0])
                        response_phase = float(numbers[1])
                        final_phase = float(numbers[2])
                        pfinal_phase = float(numbers[3])
                        final_ci = float(numbers[4])
                        pfinal_ci = float(numbers[5])
                        round_1_time = float(numbers[6])
                        reply_1_time = float(numbers[7])
                        round_2_time = float(numbers[8])
                        reply_2_time = float(numbers[9])






                        #constants pulled from the qorvo API
                        FREQ_OFFSET_MULTIPLIER = (998.4e6 / 2.0 / 1024.0 / 131072.0)
                        HERTZ_TO_PPM_MULTIPLIER_CHAN_5 = (-1.0e6 / 6489.6e6)
                        DWT_TIME_UNITS = (1.0 / 499.2e6 / 128.0) #15.65e-12s per tick of the radio


                        #new experiment: residue cancellation using phase values

                        final_freq_offset_hz = FREQ_OFFSET_MULTIPLIER * final_ci

                        #the time between the two rx samples at the receiver
                        #sample_delay = (round_1_time + reply_2_time) * DWT_TIME_UNITS
                        sample_delay = 4e-3
                        wrap_factor = 1/(6489.6e6)/sample_delay*1e6

                        #determine ambiguity
                        ambiguity = math.floor((final_freq_offset_hz * HERTZ_TO_PPM_MULTIPLIER_CHAN_5) / wrap_factor)
                        #determine drift

                        #drift_estimate = ((poll_phase - final_phase) / (2 * math.pi) + ambiguity) * wrap_factor
                        drift_estimate = ((final_phase - pfinal_phase) / (2 * math.pi) + ambiguity) * wrap_factor
                        drift_estimate %= wrap_factor

                        #when using poll and final instead of final and post-final, this is needed... I don't know why.
                        #we also get pi phase shifting with it. Also don't know why.
                        #drift_estimate /= 2

                        foffset3 = 2*math.pi*6489.6e6*drift_estimate*1e-6*sample_delay

                        print(f"foffset3: {foffset3}")




                        #phase wrap
                        if(poll_phase < 0):
                            poll_phase += 2 * math.pi
                        if(response_phase < 0):
                            response_phase += 2 * math.pi
                        if(final_phase < 0):
                            final_phase += 2 * math.pi


                        cancled_cir_1 = poll_phase + response_phase #found_phase_b_diff
                        cancled_cir_3 = poll_phase + response_phase - foffset3 #found_phase_b_diff
                        cancled_cir_1 = cancled_cir_1 % (2 * math.pi)
                        cancled_cir_3 = cancled_cir_3 % (2 * math.pi)



              

                        update_plot_data([0], [0], #anchor mag
                                        [0], [0], #tag mag
                                        [0], [0], #anchor phase
                                        [0], [0], #tag phase
                                        #green, red, blue
                                        #drift_estimate_from_cfo, drift_estimate_from_clock, drift_estimate
                                        cancled_cir_1, 0, cancled_cir_3
                                        )





            fig.canvas.draw()
            fig.canvas.flush_events()
            time.sleep(0.01)
                



except serial.SerialException as e:
    print(f"Error opening or using serial port: {e}")



#keep the final plot open when the loop finishes
plt.ioff()
plt.show()
