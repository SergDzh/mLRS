# mLRS #

The goal of the mLRS project is an open source 2.4 GHz & 915/868 MHz LoRa-based high-performance long-range radio link, which provides transparent bidirectional serial connection combined with full remote control.

The 'm' in the project name alludes to 'Mavlink', as it will have features which optimizes performance for Mavlink systems. However, it always will also provide a transparent serial link and hence will be of wider use and by no means limited to Mavlink systems only. The 'LR' in the project name alludes to 'long range', which however should not be understood in terms of an absolute range, like 50 km or so, but - of course - as the best possible range under the given conditions. Physical laws simply say that the higher the data rate the shorter the range. So, mLRS cannot compete range-wise with systems which achieve their range by reducing data rate to the minimal, at the cost of compromises. The goal of mLRS is to achieve a high range under the condition of a relatively high data rate. 

Typical specs could be 'plenty' of full-resolution RC channels, with 50 Hz update rate and serial data rates of about 3-5 kBytes/s at 2.4 GHz.

Many LRS or radio links with telemetry exist, among them open source projects such as SiK radios, OpenLRS, ExpressLRS, but also befinitiv wifibroadcast based projects like OpenHD or Ruby, closed source hobbyist projects such as UltimateLRS, QczekLRS, as well as commercial systems such as DragonLink, RFD900, Dronee Zoon, Siyi, but also TBS Crossfire and alike.

However, while all these systems are truely excellent and achieve their goals, and some of them are indeed close to what the project aims at, none of them checks all points, like 
- relatively cheap
- 2.4 GHz and 915/868 MHz
- LoRa
- full-duplex serial link with sufficient data rate
- plenty full-size RC channels
- open source
- rich features for Mavlink systems

Hence this project. In addition, as another main feature, we want it to 

- integrate with MAVLink for OpenTx

which will yield the most fluid user experience.

## Disclaimer ##

You of course use the project fully at your own risk.

## Project Status ##

The project is work in progress, and there is still a mile to go before one would call it mature.

The basic features however, i.e., the RC link and serial link, are quite stable. It also provides already a high level of usability such as a variety of options for input/output, parameter setting via the transmitter, and it integrates well with the MAVLink for OpenTx project.

It supports the SX1280, SX1276, and SX1262 Semtech chips, and thus the 2.4 GHz and 915/868 MHz frequency bands.

The RC channels layout is as follows:
- 8 channels with 11 bit resolution (CH1 - CH8), 4 of them with a higher reliability margin (CH1 - CH4)
- 4 channels with 8 bit resolution (CH9 - CH12)
- 4 channels with three steps (CH13 - CH16), 2 of them with a higher reliability margin (CH13, CH14)

It provides these operation modes:
- 50 Hz Mode<br>
  frequency bands: 2.4 GHz (SX1280 chip)<br>
  RC channels: 8 x 11 bit + 4 x 8 bit + 4 x three-step<br>
  uplink serial rate: 3200 Bytes/sec<br>
  downlink serial rate: 4100 Bytes/sec<br>
  receiver sensitivity: -105 dBm
- 31 Hz Mode<br>
  frequency bands: 2.4 GHz, 915/868 MHz (SX1280 and SX1262 chips)<br>
  RC channels: 8 x 11 bit + 4 x 8 bit + 4 x three-step<br>
  uplink serial rate: 2000 Bytes/sec<br>
  downlink serial rate: 2562 Bytes/sec<br>
  receiver sensitivity: -108 dBm
- 19 Hz Mode<br>
  frequency bands: 2.4 GHz, 915/868 MHz (SX1280, SX1276, SX1262 chips)<br>
  RC channels: 8 x 11 bit + 4 x 8 bit + 4 x three-step<br>
  uplink serial rate: 1207 Bytes/sec<br>
  downlink serial rate: 1547 Bytes/sec<br>
  receiver sensitivity: -112 dBm

Further features:
- full transmitter and receiver diversity: The Tx and Rx modules which provide two Semtech Lora chips provide full diversity. This really improves link quality in the far range.
- all options selectable via parameters: There is no need to recompile the firmware for a given board or reflash the firmware in order to change an option or parameter setting. 
- the receiver parameters can be set from within the transmitter; no need to mess with the receiver in any ways.
- the transmitter and receiver parameters can be set via a LUA script or a CLI.
- bind mode for binding "unknown" receivers to the transmitter.
- support for buzzer, oled & five-way button, serial2. 
- the Tx and Rx modules can be configured through the parameters for a wide range of applications and use cases. For a pictoral representation of some typical examples see [mLRS Setup examples](https://www.rcgroups.com/forums/showpost.php?p=48821735&postcount=332).
- support of CRSF and ArduPilot Passthrough protocol; enables using the Yaapu Telemetry app on standard radios.

## Community ##

Discussion thread at rcgroups: https://www.rcgroups.com/forums/showthread.php?4037943-mLRS-Lora-based-Mavlink-oriented-open-source-radio-link

Discord server by LELE2022: https://discord.gg/vwjzCD6ws5

## Range ##

The range which one may expect can be estimated from the standard math; the [ImmersionRc RF Link Range](https://www.immersionrc.com/rf-calculators/) calculator comes in very handy here. Let's assume: power = 20 dBm (100 mW), antenna gain = 2 dBi, link margin = 12 dB (note: 12 dB link margin is conservative). Then:

| | 50 Hz | 31 Hz | 19 Hz
| --- | --- | --- | ---        
| 2.4 GHz | 7 km | 10 km | 15 km
| 868/915 MHz | - | 26 km | 42 km

Only very few range testes were reported so far (and only for 2.4 GHz/50 Hz). They are however consistent with the estimated ranges. Also note that mLRS supports full diversity, which when enabled has been found to significantly improve performance at lower link budget, i.e., allow to operate at even larger ranges.

## Software: Installation Bits and Bops ##

This is a STM32CubeIDE project. I don't have yet much experience with this framework, and it seems it is not ideal for shared projects. This procedure should work:

Let's assume that the project should be located in the folder C:/Me/Documents/Github/mlrs.
 
1. Clone and setup the project files
- open a command line processor
- cd into `C:/Me/Documents/Github` (not C:/Me/Documents/Github/mlrs !)
- `git clone https://github.com/olliw42/mLRS.git mlrs`
- `cd mlrs`
- run `run_setup.py`. This does two steps: initializes submodules, and generates mavlink library files.

For cloning you of course can use any other tool you like, but ensure that the submodules are also retrieved (git submodule --init --recursive).

2. STM32CubeIDE
- download and install STM32CubeIDE
- start STM32CubeIDE
- in Launcher select Workspace by hitting [Browse...] button, and browse to `C:/Me/Documents/Github/mlrs/mLRS`. Hit [Launch] button. ***Note***: it is not C:/Me/Documents/Github/mlrs but C:/Me/Documents/Github/mlrs/mLRS! If you proceed with the wrong path then there will be a compile error "undefined reference to main_main()"!
- in the IDE's top bar go to `File->Open Projects from File System`
- in the Importer select Import source by hitting [Directory...] button, and browse to the desired project. E.g. select `C:/Me/Documents/Github/mlrs/mLRS/rx-diy-board01-f103cb`. Hit [Finish] button.
- change from Debug to Release configuration: Go to the 'hammer' icon in the top icon bar, click on the down arrow right to it, and select `Release`. ***Note***: if you don't do that then there will be a compile error "undefined reference to main_main()"!
- open the file `mlrs-rx.cpp` or `mlrs-tx.cpp` into the editor
- compiling should work now: Go to the green 'right-pointing triangle' icon in the top icon bar and click it
- Repeat the last five steps for each board you are interested in

<img src="https://user-images.githubusercontent.com/6089567/154903396-25f62bf6-573a-4b80-9720-a0ad4a21f291.jpg" width="480">

The STM32CubeIDE has its weirdness, so you may have to get used to it. 

In case of issues with this procedure, don't hesitate to join the discussion thread at rcgroups, or submit an issue in the github repository.

#### Dependencies ####

You need to have git and python3 installed.

## Hardware ##

Hardware is a problem currently. One might be tempted to think that all the recent commercial ExpressLRS hardware should be good platforms, but this is unfortuantely not so. The ESP's they use simply do not offer the peripherals which are desired for mLRS TX modules, hence I started with STM32 as main platform. I am not against ESP however, to the contrary. So if anyone wants to add ESP32 support please join.

The code so far supports:
- Frsky R9M transmitter and R9MX and R9MM receiver modules
- Siyi FM30 system (early version only, those with STM32 chips; the TX module needs few small hardware modifications, see https://github.com/olliw42/mLRS/issues/4#issuecomment-1030601900)
- several DIY boards you can find in https://github.com/olliw42/mLRS-hardware

In the 915/868 MHz range, the Frsky R9M & R9MX system provides a simple and readily available entry to mLRS. In this sense it is the best option available currently. Its big disadvantage is however that the receiver's transmission power is quite low and telemetry range thus relatively short. In the 2.4 GHz range, the DIY options are currently the way to go.

Don't hesitate to join the discussion thread at rcgroups for more details.

## Further Documentations ##

You find more documentation on the following pages:
- [Binding](docs/BINDING.md)
- [Configuration Parameters](docs/PARAMETERS.md)
- [CLI Commands](docs/CLI.md)
- Set up [CRSF Telemetry and Yaapu Telemetry App](docs/CRSF.md)
- Set up [MAVLink for OpenTx](docs/MAVLINK_FOR_OPENTX.md)
