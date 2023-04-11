#!/bin/bash

# acquire root
echo -e "\033[32m ACQUIRING ROOT \033[0m"
sudo su

# install dependencies
echo -e "\033[32m INSTALLING DEPENDENCIES \033[0m"
# TODO

# compile all files
echo -e "\033[32m COMPILING FILES \033[0m"
make

# install the kernel module to the kernel
echo -e "\033[32m INSTALLING KERNEL MODULE \033[0m"
insmod assoofs.ko

# create a test iso
echo -e "\033[32m CREATING TEST ISO \033[0m"
dd bs=4096 count=100 if=/dev/zero of=assoofs.iso
./mkassofs assoofs.iso

# mount the iso
echo -e "\033[32m MOUNTING TEST ISO \033[0m"
mkdir mnt
mount -o loop -t assoofs assoofs.iso mnt

# check messages
echo -e "\033[32m PRINTING MESSAGES \033[0m"
dmesg

# show a message
echo "DONE"