# About folder

This folder is for trying to implement the full working TWR as outlined in the paper: ***Push the Limit of Highly Accurate Ranging on Commercial UWB Devices***


I think I can eliminate the need for a post-final packet transmission by using the timestamps between poll and final instead of final and post-final



I have 3 phase angles measured in radians.
Let's call them:
A
B
C
All three are bounded between [0,2pi].

The difference between A and B is some percentage (say, 20%) of the difference between B and C.
I can't make any other guarantees about the values; they're essentially random.

I need to figure out how to derive B given A and C. Is this possible?



This isn't a 
I have 4 phase angles measured in radians.
I'm going to call call them:
A
B
C
D
All are bounded between [0,2pi].
I know A, C, and D

The difference between A and B is some known percentage (say, 20%) of the difference between B and C.
The difference between A and B is a different known percentage (say, 10%) of the difference between B and D.
The percentages will always be the same; they're a constant that I can control.
I can't make any other guarantees about the values; they're essentially random.

I know with just angles A, B, and C, I don't have enough information to derive a single answer; I'll always have 2.
If I add in angle D, do I have enough info to constrain B to a single angle? How would I do this?





I have a DW3000 I'm trying to do phase recovery with. I want to do drift cancellation as outlined in this snippet of paper I've shared, but I want to remove the time constraint where (t7-t5)=(t3-t2). That is to say, I want to be able to send t7 at any time I wish, so long as I keep track of the total difference between t7 and t5. I have already attempted this experimentally, but phase ambiguity meant there were two possible correction values for every ratio I did. If I added another post-post-final message to the mix, would having two values be enough to eliminate the phase ambiguity and allow me to derive (t3-t2)?




With the DW3000, I have 2 numbers representing the time between transmissions from one radio and the time between receptions for the other one.
Both are still in the same units as whatever's returned by get_rx_timestamp_u64().
I want to observe the difference in time to establish the clock drift ratio between one and the other. Eventually, I want to convert this ratio into a frequency offset. I'm using channel 5 to do these ranging operations.
How can I do this?





