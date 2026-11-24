
import serial
import threading
import time
from collections import deque

import dearpygui.dearpygui as dpg

# ---------------- Configuration ----------------
BAUD_RATE = 460800
MAX_SAMPLES = 500
UPDATE_INTERVAL = 0.02  # 20 ms

# Change this to your serial port
SERIAL_PORT = "COM17"  # Windows example
# SERIAL_PORT = "/dev/ttyUSB0"  # Linux example

# ---------------- Data ----------------
data_x = deque(maxlen=MAX_SAMPLES)
data_y = deque(maxlen=MAX_SAMPLES)

sample_count = 0
running = True
ser = None


# ------------------Averaging class
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




# ---------------- Serial Reader ----------------
def serial_reader():
    global sample_count, ser

    average = MovingAverage(6)

    try:
        ser = serial.Serial(
            port=SERIAL_PORT,
            baudrate=BAUD_RATE,
            timeout=0.1
        )
        print(f"Connected to {SERIAL_PORT} at {BAUD_RATE} baud")

    except serial.SerialException as e:
        print(f"Serial connection error: {e}")
        return

    while running:
        try:
            if ser.in_waiting:
                line = ser.readline().decode(
                    "utf-8", errors="ignore"
                ).strip()

                if not line:
                    continue

                try:
                    value = float(line)

                    # Ignore NaN and infinity
                    if not (float("-inf") < value < float("inf")):
                        continue

                    #ignore huge outliers
                    if(value < -100 or value > 100):
                        continue

                    #smooth the input
                    value = average.add(value)

                    sample_count += 1
                    data_x.append(sample_count)
                    data_y.append(value)

                except ValueError:
                    # Ignore non-numeric serial input
                    continue

        except serial.SerialException as e:
            print(f"Serial read error: {e}")
            break

    if ser and ser.is_open:
        ser.close()


# ---------------- GUI ----------------
dpg.create_context()

with dpg.window(label="Serial Plotter", width=900, height=600):

    dpg.add_text(
        f"Serial Plotter | {BAUD_RATE} baud",
        color=(100, 200, 255)
    )

    dpg.add_separator()

    with dpg.plot(
        label="Serial Data",
        height=-1,
        width=-1
    ):
        dpg.add_plot_legend()

        dpg.add_plot_axis(
            dpg.mvXAxis,
            label="Sample",
            tag="x_axis"
        )

        with dpg.plot_axis(
            dpg.mvYAxis,
            label="Value",
            tag="y_axis"
        ):
            dpg.add_line_series(
                [],
                [],
                label="Serial Value",
                tag="serial_series"
            )

    dpg.add_text("Waiting for serial data...", tag="status")

dpg.create_viewport(
    title="Dear PyGui Serial Plotter",
    width=1000,
    height=700
)

dpg.setup_dearpygui()
dpg.show_viewport()

# Start serial thread
thread = threading.Thread(
    target=serial_reader,
    daemon=True
)
thread.start()

# ---------------- Main Loop ----------------
try:

    while dpg.is_dearpygui_running():

        if data_y:
            # Update graph
            x_values = list(data_x)
            y_values = list(data_y)

            dpg.set_value(
                "serial_series",
                [x_values, y_values]
            )

            # Keep X-axis following the newest data
            if len(x_values) >= MAX_SAMPLES:
                x_min = x_values[0]
                x_max = x_values[-1]
            else:
                x_min = 0
                x_max = MAX_SAMPLES

            dpg.set_axis_limits(
                "x_axis",
                x_min,
                x_max
            )

            # Automatically scale Y-axis
            #dpg.fit_axis_data("y_axis")

            dpg.set_item_label(
                "status",
                f"Samples received: {sample_count} | "
                f"Latest value: {y_values[-1]:.6f}"
            )

        dpg.render_dearpygui_frame()
        time.sleep(UPDATE_INTERVAL)

finally:
    running = False
    if thread.is_alive():
        thread.join(timeout=1)

    if ser and ser.is_open:
        ser.close()

    dpg.destroy_context()
