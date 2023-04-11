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

# create a test file
echo -e "\033[32m CREATING TEST FILE \033[0m"
# TODO

# show a message
echo "DONE"