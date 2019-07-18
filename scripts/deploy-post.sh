#! /bin/bash

# build commons

# build cx
#cd /home/utnso/lissandra/cx
#make clean && make debug
#make runtests

# build nodes
cd /home/utnso/lissandra/ker
make clean && make debug

cd /home/utnso/lissandra/mem
make clean && make debug

cd /home/utnso/lissandra/lfs
make clean && make debug
