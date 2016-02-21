  1. Install macports - http://macports.org/install.php
  1. $ sudo port install wine
  1. $ sudo port install qemu
  1. Download runPhantom.zip. extract.
  1. $ cd runPhantom && wine bin/pfsformat.exe phantom.img
  1. $ sh phantom.sh

phantom.sh:

```
USB="-usb -usbdevice mouse"
VIO="-drive file=vio.img,if=virtio,format=raw"
Q_PORTS="-parallel file:lpt_01.log  -serial file:serial0.log"
Q_NET="-net user -tftp tftp -net nic,model=pcnet"
Q_MACHINE=""
Q_DISKS="-boot a -no-fd-bootchk -fda img/grubfloppy.img -hda snapcopy.img -hdb phantom.img"
Q_VGA="-vga std"

rm serial0.log.old
mv serial0.log serial0.log.old

/opt/local/bin/qemu $Q_VGA $Q_KQ $Q_MACHINE $Q_PORTS  $Q_DISKS  $Q_NET $VIO $USB
```