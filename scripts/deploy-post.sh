#! /bin/bash

# build commons
cd /home/utnso/so-commons-library
make
sudo make install

# build cx
cd /home/utnso/lissandra/cx
make clean && make debug
make runtests

# build ker
cd /home/utnso/lissandra/ker
make clean && make debug

# build mem
cd /home/utnso/lissandra/mem
make clean && make debug

# build lfs
cd /home/utnso/lissandra/lfs
make clean && make debug
