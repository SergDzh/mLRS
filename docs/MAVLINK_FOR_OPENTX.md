# mLRS Documentation: MAVLink for OpenTx #

([back to main page](../README.md))

This page describes how to set up a mLRS system for OpenTx radios running the MAVLink for OpenTx firmware.

Four things need to be done, (1) The OpenTx radio needs to be flashed with the MAVLink for OpenTx firmware, (2) the mLRS Tx module needs to be put into "mBridge mode", (3) the flight controller needs to be set up for MAVLink on a serial port, and (4) the radio needs to be configured. 

Step (1) is beyond the scope of this article; please consult the project's discussion channels.

In principle, there is no specific configuration of the mLRS receiver neccessary. It is however recommended to set the receiver into "mavlink mode", as described below.

Note: An ArduPilot flight controller is assumed. For PX4 it needs to be tested and seen. iNAV won't work AFAIK, as iNAV is not a proper MAVLink component.


## mLRS Tx Module Setup

- Tx Ch Source = mbridge
- Tx Ser Dest = mbridge
- Tx Ser Link Mode = mavlink
- Tx Snd RadioStat = off (yes, off!)

Note: There are situations in which it can be usefull to enable "Tx Snd RadioStat", but you should do this only if you know what you are doing.


## ArduPilot Setup

Configuration of a serial for MAVLink v2

- SERIALx_BAUD = 57 
- SERIALx_OPTIONS = 0
- SERIALx_PROTOCOL = 2


## OpenTx Radio Setup

- MDL->MODEL SETUP->External RF->Mode = mBridge



## mLRS Rx Module Setup

These configurations are not strictly neccesary, but strongly recommended.

- Rx Ser Link Mode = mavlink
- Rx Snd RadioStat = w txbuf

