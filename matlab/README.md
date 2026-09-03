# What a weird readme. Cam I have it?

The matlab code looks like it does everything outlined in the corresponding research paper.

As for the ESP32 firmware... that's TBD.

Some useful info though:

```
%data format for an anchor, there are 3 per row, which are the other three anchors
%[CIR real (signed int)]	[CIR IM (signed int)]	[phase calib]	[rx preamble]	[gain]	[Time-of-flight]	[the ID of the anchor we were talking to]
%039d,						0342,					7f,				1f,				0019,	3534c61dc9,			1

%data format for a tag, there are 4 per row, which are all four other anchors
%[CIR real (signed int)]	[CIR IM (signed int)]	[phase calib]	[rx preamble]	[gain]	[Time-of-flight]	[carrier frequency offset]	[the ID of the anchor we were talking to]
%0389,						f6f9,					01,				3b,				01a1,	93412952a4,			00000064,					0

```


The whole MULoc concept is pretty interesting...
Only the anchors transmit UWB data. Tags are completely passive.
This allows a ton of tags to range off the anchors without clogging up the airspace.

I'm still not *completely* certain how the full results are gotten from this data, but it's a start.






