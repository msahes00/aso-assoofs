#!/bin/bash

# show a confirmation prompt
function showConfirm() {

	read -p "$1: " confirm

	# option can have two values 1 (no, by default) or 0 (yes) 
	option="1"

	case "$confirm" in 

		y)
			;&
		Y)
			;&
		ye)
			;&
		yE)
			;&
		Ye)
			;&
		YE)
			;&
		yes)
			;&
		yeS)
			;&
		yEs)
			;&
		yES)
			;&
		Yes)
			;&
		YeS)
			;&
		YEs)
			;&
		YES)
			;&
		s)
			;&
		S)
			;&
		si)
			;&
		sI)
			;&
		Si)
			;&
		SI)
			# this will execute on any of the above cases
			option="0"
			;;

	esac

}

# acquire root
echo -e "\033[32m ACQUIRING ROOT \033[0m"
sudo /bin/bash -c ":"

# uninstall the kernel module to the kernel
echo -e "\033[32m UNINSTALLING KERNEL MODULE \033[0m"
sudo rmmod assoofs.ko


# clean workspace
echo -e "\033[32m CLEANING FILES \033[0m"
sudo make clean

# prompt to delete the test iso
echo -e "\033[32m REMOVING TEST ISO \033[0m"

showConfirm "Unmount test iso? (yes / no)"
if [ $option -eq "0" ] ; then

	sudo umount assoofs.iso mnt
	rmdir mnt

	showConfirm "Delete test iso? (yes / no)"
	if [ $option -eq "0" ] ; then

		rm assoofs.iso

	fi

fi

# prompt to uninstall dependencies
echo -e "\033[32m UNINSTALLING DEPENDENCIES \033[0m"

showConfirm "Uninstall dependencies? (yes / no)"
if [ $option -eq "0" ] ; then

	sudo apt autoremove build-essential dwarves -y
	# TODO: update dependencies
	#sudo add-apt-repository --remove universe -y
	#rm /sys/kernel/btf/vmlinux /usr/lib/modules/`uname -r`/build/

fi

# check messages
echo -e "\033[32m PRINTING MESSAGES \033[0m"
sudo dmesg | grep "assoofs"

# show a message
echo "DONE"