import random
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.figure import Figure
from matplotlib.axes import Axes
# 1. Initialize the figure and axes

figax: tuple[Figure, Axes] = plt.subplots()

fig = figax[0]
ax = figax[1]

x_data: list[float] = []
y_data: list[float] = []

# 2. Define the animation function
def animate(i):

    #i increments with the number of times this method is run

    x_data.clear()
    y_data.clear()

    for i in range(20):
        x_data.append(i)
        y_data.append(random.randint(0,10))
    

    #clear the previous frame and redraw the updated line
    ax.cla()
    ax.plot(x_data, y_data, marker='o', color='b')
    
    #re-apply labels so they don't get wiped by ax.cla()
    ax.set_title("Live Data Stream")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Value")

#create the animation object (interval is in milliseconds)
#and keep a reference to 'ani', otherwise Python will garbage collect it.
ani = FuncAnimation(fig, animate, interval=100)

plt.tight_layout()
plt.show()
