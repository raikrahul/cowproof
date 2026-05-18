#!/bin/bash
cd /home/r/Desktop/cowproof/src
echo "obj-m += refcount_proof.o" >> Makefile
make > /dev/null 2>&1
gcc -o 05_cow_refcount_test 05_cow_refcount_test.c
sudo dmesg -c > /dev/null
sudo insmod refcount_proof.ko target_pid=1 target_va=1 # just to load it
sudo rmmod refcount_proof
