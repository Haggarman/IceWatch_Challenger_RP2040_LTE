# IceWatch_Challenger_RP2040_LTE
 IOT Temperature Alarm System using hologram.io cellular service.
![20221101_180118](https://github.com/Haggarman/IceWatch_Challenger_RP2040_LTE/assets/96515734/f2a9d9d8-f910-4ca7-be98-d4896760a9e1)

## Basic Features:
* Now monitors and messages about Power Loss, in addition to Temperature Switch Alarm.
* Sends TCP messages to Hologram. Which then sends out SMS alerts to the cellphones that want them.
* You can power on with dip switch #4 set to on to skip SARA modem init.
* You can hold down button C on the OLED to power down the SARA modem.
* Green LED flashes when a message send is pending, otherwise it breathes lighter and dimmer.
* Subtle pixel shifting of text to try to head off OLED burn-in.
* Passes Arduino USB terminal text over to SARA when the state machines are not busy with their command.
* Echoes text back to terminal if it originated from terminal.

## Issues:
* If you press Reset button on the OLED, the display will blank. It does not reset the RP2040. This is an Adafruit choice.
* Reset for the RP2040 is unlabeled, but it is the button nearest the USB connector on the side.
* The Adafruit battery is protected from over discharge, but the RP2040 could go haywire at low voltage (untested).
* LOL no message rate limiting if sensor would continuously be toggling. YOLO. (Hologram would ban after a while anyway).
* Serial Debug text stuff not quite removed yet. Nor am I comfortable removing it yet.

## Required Libraries:
 Adafruit SH110X, Adafruit GFX Library

##
 I have to document this one-time setup.
Command	| Description
-- | --
AT | OK is what it should respond with after typing A T and then pressing enter. Both Linux or Windows style Carriage Returns / Line Feeds are acceptable from a serial terminal.
AT+UPSV=0	| There is a default power saving setting which disables the UART after 6 seconds of inactivity. Leaving that on will drive you insane. Also good luck typing this within the first 6 seconds, buddy. Better keep spamming AT.
AT+CPSMS=0 | Disable PSM (Power saving mode OFF, if only it would wake up properly this would be useful).
AT+UMNOPROF=100	| The Challenger startup aggressively sets Standard Europe automatically so just go with this profile.
AT+CFUN=15 | soft reset
AT+UBANDMASK=0,2074	| Bands available in the US are 2,4,5,12,13. That would be 6170.
AT+UBANDMASK=1,2074	| However band 13 is causing issues at install site, so using 2074.
AT+CFUN=15	| soft reset
AT+CGDCONT=1,"IP","hologram"	| Set the APN
AT+COPS=0	| Automatic Operator Selection (this is causing me problems, only AT&T wants to talk but it hops over and gets denied anyway.)
AT+CFUN=15	| Reset modem. Parameters get saved only during soft reset command or soft shutdown.
AT+URAT=7	| Might help to set LTE Cat M1 technology only.
AT+CFUN=15	| requires a soft reset as usual
AT+CPWROFF	| When you want to remove power you should tell modem to shut off first. Wait a few seconds after OK. Hard reset when downloading to the RP2040 will cause issues unless you **issue AT+CPWROFF first!**
