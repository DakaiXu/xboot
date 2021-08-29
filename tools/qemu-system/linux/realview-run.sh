#!/bin/sh
#

# The qemu's root directory.
export QEMU_DIR=$(cd `dirname $0` ; pwd)

if [ ! -e ~/.xboot/sdcard.img ]; then
	mkdir -p ~/.xboot;
	unzip ${QEMU_DIR}/sdcard.zip -d ~/.xboot > /dev/null;
fi

# Run qemu
exec /home/jjj/test/qemu/build/qemu-system-arm -M realview-pbx-a9 -m 256M -smp 4 -name "ARM RealView Platform Baseboard Explore for Cortex-A9" -rtc base=localtime -serial stdio -sd ~/.xboot/sdcard.img -net nic,netdev=net0,macaddr=88:88:88:11:22:33,model=smc91c111 -netdev user,id=net0,hostfwd=tcp::8080-:80 -kernel ${QEMU_DIR}/../../../output/xboot
