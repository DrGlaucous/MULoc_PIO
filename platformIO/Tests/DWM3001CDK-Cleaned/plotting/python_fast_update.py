import time
import random
import matplotlib.pyplot as plt
from matplotlib.figure import Figure
from matplotlib.axes import Axes


#turn on interactive mode
plt.ion()

#get plot sub-parts
figax: tuple[Figure, Axes] = plt.subplots()
fig = figax[0]
ax = figax[1]

#arrays of data to populate
x_data: list[float] = []
y_data: list[float] = []


#create an initial empty line object
line, = ax.plot([], [], color='r', marker='x')


# Pre-set limits if you know them, or auto-scale later
ax.set_xlim(0, 50)
ax.set_ylim(0, 100)

for i in range(50):

    
    x_data.append(i)
    y_data.append(random.randint(0, 100))
    
    # Update the data inside the line object directly
    line.set_xdata(x_data)
    line.set_ydata(y_data)
    
    # Adjust axes limits dynamically if data exceeds views
    ax.relim()
    ax.autoscale_view()
    
    # Force redraw and pause briefly to let the GUI refresh
    fig.canvas.draw()
    fig.canvas.flush_events()
    time.sleep(0.1)

# Keep the final plot open when the loop finishes
plt.ioff()
plt.show()
