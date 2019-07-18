#! /bin/bash

res_name="tests-final"

vm_1="127.0.0.1"
vm_2="127.0.0.2"
vm_3="127.0.0.3"
vm_4="127.0.0.4"

rm -rf ../res/cfg
rm -rf ../res/scripts

cp -r ../res/$res_name/* ../res

find ../res/cfg -type f -name "*.cfg" -exec sed -i 's/{VM_1}/'$vm_1'/g' {} \;
find ../res/cfg -type f -name "*.cfg" -exec sed -i 's/{VM_2}/'$vm_2'/g' {} \;
find ../res/cfg -type f -name "*.cfg" -exec sed -i 's/{VM_3}/'$vm_3'/g' {} \;
find ../res/cfg -type f -name "*.cfg" -exec sed -i 's/{VM_4}/'$vm_4'/g' {} \;
