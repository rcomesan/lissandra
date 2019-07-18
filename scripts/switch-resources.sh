#! /bin/bash

source defines.sh

rm -rf ../res/cfg
rm -rf ../res/scripts

cp -r ../res/$res_name/* ../res

find ../res/cfg -type f -name "*.cfg" -exec sed -i 's/{VM_1}/'$vm_1'/g' {} \;
find ../res/cfg -type f -name "*.cfg" -exec sed -i 's/{VM_2}/'$vm_2'/g' {} \;
find ../res/cfg -type f -name "*.cfg" -exec sed -i 's/{VM_3}/'$vm_3'/g' {} \;
find ../res/cfg -type f -name "*.cfg" -exec sed -i 's/{VM_4}/'$vm_4'/g' {} \;

read -p "Done..."
