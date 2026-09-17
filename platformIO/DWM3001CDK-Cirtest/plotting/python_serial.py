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

canceled_phase_ln, = canceled_graph.plot([], [], color='g',)
canceled_y_array: list[float] = []

#pre-set limits if you know them, or auto-scale later
#magnitude_graph.set_xlim(0, 20)
#magnitude_graph.set_ylim(0, 3500)
#phase_graph.set_xlim(0, 20)
phase_graph.set_ylim(0, 8)

canceled_graph.set_ylim(0, 8)

#take new x and y data and put it on the graph
def update_plot_data(
        anchor_mag_x: list[float], anchor_mag_y: list[float],
        tag_mag_x: list[float], tag_mag_y: list[float],
        anchor_phase_x: list[float], anchor_phase_y: list[float],
        tag_phase_x: list[float], tag_phase_y: list[float],
        canceled_y: float
        ):

    #ensure inputs are equal in length
    if(len(anchor_mag_x) != len(anchor_mag_y) or
       len(tag_mag_x) != len(tag_mag_y) or
       len(anchor_phase_x) != len(anchor_phase_y) or
       len(tag_phase_x) != len(tag_phase_y)
       ):
        return

    canceled_y_array.append(canceled_y)
    if(len(canceled_y_array) > 50):
        canceled_y_array.pop(0)

    canceled_x_vals: list[float] = []
    for i in range(len(canceled_y_array)):
        canceled_x_vals.append(i)

    canceled_phase_ln.set_xdata(canceled_x_vals)
    canceled_phase_ln.set_ydata(canceled_y_array)

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

                        anchor_cir = parse_cir_line(parts[1])
                        tag_cir = parse_cir_line(parts[2])

                        #phase wrap
                        for i in range(len(anchor_cir[0])):
                            if(anchor_cir[0][i] < 0):
                                anchor_cir[0][i] += 2 * math.pi
                            if(tag_cir[0][i] < 0):
                                tag_cir[0][i] += 2 * math.pi



                        #I think I can use collect to make this go faster... oh, well.
                        x_vals: list[float] = []
                        for i in range(len(anchor_cir[0])):
                            x_vals.append(float(i))

                        cancled_cir = anchor_cir[0][9] + tag_cir[0][9]
                        cancled_cir = cancled_cir % (2 * math.pi)

                        delta_cir = cancled_cir - last_canceled_cir
                        last_canceled_cir = cancled_cir
                        print(cancled_cir)

                        #test: offset by 1/2 a phase if this happens
                        #if(delta_cir > math.pi * 0.5 and delta_cir < math.pi * 0.5):
                        #    cancled_cir += math.pi
                        #    cancled_cir = cancled_cir % (2 * math.pi)

                        

                        update_plot_data(x_vals, anchor_cir[1], #anchor mag
                                        x_vals, tag_cir[1], #tag mag
                                        x_vals, anchor_cir[0], #anchor phase
                                        x_vals, tag_cir[0], #tag phase
                                        cancled_cir
                                        )





            fig.canvas.draw()
            fig.canvas.flush_events()
            time.sleep(0.01)
                



except serial.SerialException as e:
    print(f"Error opening or using serial port: {e}")



#keep the final plot open when the loop finishes
plt.ioff()
plt.show()
