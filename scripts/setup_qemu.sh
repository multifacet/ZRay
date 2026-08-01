#Attempt 1
#==========================================================================
#Working dir
#mkdir riscv64-linux
#cd riscv64-linux

#sudo apt install autoconf automake autotools-dev curl libmpc-dev libmpfr-dev libgmp-dev \
#                 gawk build-essential bison flex texinfo gperf libtool patchutils bc \
#                 zlib1g-dev libexpat-dev git libpixman-1-dev libslirp-dev  -y
#
#git clone https://github.com/qemu/qemu
#git clone https://github.com/torvalds/linux
#git clone https://git.busybox.net/busybox
#
#cd qemu
#git checkout stable-8.0 
#./configure --target-list=riscv64-softmmu --enable-slirp
#make -j $(nproc)
#sudo make install
#
#cd ../linux
#git checkout v6.4
#make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- defconfig
#make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- -j $(nproc)
#
#cd ../busybox
#CROSS_COMPILE=riscv64-unknown-linux-gnu- make defconfig
#CROSS_COMPILE=riscv64-unknown-linux-gnu- make -j $(nproc)
#
#sudo qemu-system-riscv64 -nographic -machine virt \
#     -kernel linux/arch/riscv/boot/Image -append "root=/dev/vda ro console=ttyS0" \
#     -drive file=busybox/busybox,format=raw,id=hd0 \
#     -device virtio-blk-device,drive=hd0
##==========================================================================

#Attempt 2
##==========================================================================
#wget "https://gitlab.com/api/v4/projects/giomasce%2Fdqib/jobs/artifacts/master/download?job=convert_riscv64-virt" -O debian-rv64.zip
#mkdir debian-rv64
#cd debian-rv64
#unzip ../debian-rv64.zip
#
#cd ..

# Grab the URL from https://packages.debian.org/sid/u-boot-qemu
#wget "http://ftp.us.debian.org/debian/pool/main/u/u-boot/u-boot-qemu_2023.01+dfsg-2_all.deb" -O u-boot-qemu.deb
#mkdir u-boot-qemu
#cd u-boot-qemu
#ar -x ../u-boot-qemu.deb
#tar xvf data.tar.xz
#cp ./usr/lib/u-boot/qemu-riscv64_smode/uboot.elf ./uboot.elf

#cd ..

#qemu-img create -o backing_file=./debian-rv64/dqib_riscv64-virt/image.qcow2,backing_fmt=qcow2 -f qcow2 overlay.qcow2
#
#qemu-system-riscv64 \
#    -machine virt \
#    -cpu rv64 \
#    -smp $(nproc) \
#    -m 100G \
#    -device virtio-blk-device,drive=hd \
#    -drive file=./riscv64-linux/dqib_riscv64-virt/image.qcow2,if=none,id=hd \
#    -device virtio-net-device,netdev=net \
#    -netdev user,id=net,hostfwd=tcp::2222-:22 \
#    -bios /usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.elf \
#    -kernel ./u-boot-qemu/uboot.elf \
#    -object rng-random,filename=/dev/urandom,id=rng \
#    -device virtio-rng-device,rng=rng \
#    -append "root=LABEL=rootfs console=ttyS0" \
#    -nographic 
    #-drive file=overlay.qcow2,if=none,id=hd \
##==========================================================================
#Attempt 3 (Works! on ubuntu), adjust the image resize however you like

sudo apt-get install opensbi qemu-system-misc u-boot-qemu

wget https://cdimage.ubuntu.com/releases/22.04.2/release/ubuntu-22.04.3-preinstalled-server-riscv64+unmatched.img.xz

xz -dk ubuntu-22.04.3-preinstalled-server-riscv64+unmatched.img.xz

qemu-img resize -f raw ubuntu-22.04.3-preinstalled-server-riscv64+unmatched.img +40G #Currently increased by 40GB --> ~45GB total

qemu-system-riscv64 \
    -machine virt -nographic \
    -m 100G -smp 8 \
    -bios /usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.bin \
    -kernel /usr/lib/u-boot/qemu-riscv64_smode/uboot.elf \
    -device virtio-net-device,netdev=eth0 -netdev user,id=eth0 \
    -device virtio-rng-pci \
    -drive file=ubuntu-22.04.3-preinstalled-server-riscv64+unmatched.img,format=raw,if=virtio \
    -device virtio-net-device,netdev=net \
    -netdev user,id=net,hostfwd=tcp::2222-:22 
