import collections
import math
import time
import dearpygui.dearpygui as dpg

# 1. Initialize fixed-length buffers for X and Y data
MAX_POINTS = 200
x_data = collections.deque(maxlen=MAX_POINTS)
y_data = collections.deque(maxlen=MAX_POINTS)

start_time = time.time()

dpg.create_context()

with dpg.window(label="Rolling Line Graph", width=800, height=500):
    with dpg.plot(label="Live Sensor Feed", height=-1, width=-1):
        dpg.add_plot_legend()
        
        # Create X and Y axes with tags so we can update their limits dynamically
        dpg.add_plot_axis(dpg.mvXAxis, label="Time (s)", tag="x_axis")
        with dpg.plot_axis(dpg.mvYAxis, label="Amplitude", tag="y_axis"):
            # Add the line series with initial empty lists
            dpg.add_line_series([], [], label="Signal", tag="series_tag")
            
        # Lock Y-axis range (optional: remove if you want auto-scaling Y)
        dpg.set_axis_limits("y_axis", -1.5, 1.5)

dpg.create_viewport(title="DearPyGui Rolling Plot", width=820, height=540)
dpg.setup_dearpygui()
dpg.show_viewport()

# 2. Main render loop
while dpg.is_dearpygui_running():
    # Calculate elapsed time and new data value
    t = time.time() - start_time
    val = math.sin(t * 3.0) + 0.2 * math.cos(t * 15.0)
    
    # Append to deque (automatically drops oldest item when full)
    x_data.append(t)
    y_data.append(val)
    
    # Update the plot series (must convert deque to list)
    dpg.set_value("series_tag", [list(x_data), list(y_data)])
    
    # Scroll the X-axis to match the current data window
    if len(x_data) > 1:
        dpg.set_axis_limits("x_axis", x_data[0], x_data[-1])
        
    dpg.render_dearpygui_frame()

dpg.destroy_context()

