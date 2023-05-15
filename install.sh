#!/bin/bash

# acquire root
echo -e "\033[32m ACQUIRING ROOT \033[0m"
sudo /bin/bash -c ":"

# install dependencies
echo -e "\033[32m INSTALLING DEPENDENCIES \033[0m"
sudo apt install build-essential -y

# compile all files
echo -e "\033[32m COMPILING FILES \033[0m"
sudo make

# install the kernel module to the kernel
echo -e "\033[32m INSTALLING KERNEL MODULE \033[0m"
sudo insmod assoofs.ko

# create a test iso
echo -e "\033[32m CREATING TEST ISO \033[0m"
sudo dd bs=4096 count=100 if=/dev/zero of=assoofs.iso
sudo ./mkassofs assoofs.iso

# mount the iso
echo -e "\033[32m MOUNTING TEST ISO \033[0m"
sudo mkdir mnt
sudo mount -o loop -t assoofs assoofs.iso mnt

# update permissions
echo -e "\033[32m UPDATING PERMISSIONS \033[0m"
sudo chmod -R 777 .

# check messages
echo -e "\033[32m PRINTING MESSAGES \033[0m"
sudo dmesg | grep "assoofs"

# show a message
echo "DONE"