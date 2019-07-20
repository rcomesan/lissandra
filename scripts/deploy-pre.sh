#! /bin/bash

sudo rm -rf /usr/lib/libcommons.so
sudo rm -rf /usr/include/commons

rm -rf /home/utnso/so-commons-library

rm -rf /home/utnso/lissandra
rm -rf /home/utnso/lissandra-checkpoint
rm -rf /home/utnso/lfs-base
rm -rf /home/utnso/lfs-prueba-kernel
rm -rf /home/utnso/lfs-compactacion
rm -rf /home/utnso/lfs-prueba-memoria
rm -rf /home/utnso/lfs-stress

mkdir /home/utnso/so-commons-library
mkdir /home/utnso/lissandra
mkdir /home/utnso/lissandra/cx
mkdir /home/utnso/lissandra/ker
mkdir /home/utnso/lissandra/mem
mkdir /home/utnso/lissandra/lfs
mkdir /home/utnso/lissandra/res
mkdir /home/utnso/lissandra/scripts
