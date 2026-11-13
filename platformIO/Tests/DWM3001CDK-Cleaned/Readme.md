# About folder

Now that I have basic phase recovery working, my next task is to implement the rest of the DSTWR scheme, including frequency hopping and ambiguity resolution.







Two conditions that result in bad final measurements:

The phase of the summed angle wraps due to noise, bumping the final calculation up or down by 2pi (10 cm)

The distance measured by DSTWR crosses over the wrap threshold (in this case, right around every 10 cm or so) without the phase jumping to match, bumping the final calculation up or down by 2pi (which translates to 10 cm)



This same issue happens with the fine ambiguity resolution, but at a smaller scale.




I'm trying to measure the distance between two radio transceivers given the time-of-flight of a signal between them and the phase angle of arrival on the receiver. The wavelength of the signal is ~10cm, and both the incoming TOF and phase angle contain some noise (TOF varies by +-5cm, and phase angle varies by +-0.5 radians).
I can get a more precise distance using the phase angle, but for distances larger than 10cm, the angle loops. To resolve this ambiguity, I combine it with the coarser time-of-flight signal.

I use the following python script to do this
```py
ambiguity_ct = math.floor(distance_twr / wavelength)
distance = (phase_angle / (2 * math.pi) + ambiguity_ct) * wavelength
```
This works in theory, but an issue arises when the distance reaches the looping "seam" around 10cm. One of two things will happen: noise in the phase angle will cause the signal to jump by +-10cm, or noise in the TWR measurements will cause the signal to jump by +-10cm. How can I limit or mitigate these phase jumps?




One hunch I have is this:
The phase angle I'm using is actually the difference between two higher frequency signals (~8ghz and ~6.5ghz) that I use to enlarge the wavelength from ~4.5cm to 10cm. Is there potentially a different way to combine or offset these wavelengths to help resolve the ambiguity issues?








