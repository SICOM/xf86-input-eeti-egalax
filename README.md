xf86-input-eeti-egalax - EETI eGalax input driver for Xorg server

The EETI eGalax touchscreen device has three variants:
- I2C connected, the Linux kernel (3.14) has a driver for it
  but it depends on OpenFirmware so it's not for PCs.
- USB connected, it emulates a HID device. xf86-input-evdev
  can use it
- serial connected, that is what this driver is for.

Please submit bugs & patches via Github:

        https://github.com/SICOM/xf86-input-eeti-egalax

The master development code repository can be found at:

        https://github.com/SICOM/xf86-input-eeti-egalax
