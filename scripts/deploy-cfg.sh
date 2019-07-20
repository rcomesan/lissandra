source defines.sh

scp -rpC  ../res/cfg utnso@$vm_1:/home/utnso/lissandra/res
scp -rpC  ../res/scripts utnso@$vm_1:/home/utnso/lissandra/res

scp -rpC  ../res/cfg utnso@$vm_2:/home/utnso/lissandra/res
scp -rpC  ../res/scripts utnso@$vm_2:/home/utnso/lissandra/res

scp -rpC  ../res/cfg utnso@$vm_3:/home/utnso/lissandra/res
scp -rpC  ../res/scripts utnso@$vm_3:/home/utnso/lissandra/res

scp -rpC  ../res/cfg utnso@$vm_4:/home/utnso/lissandra/res
scp -rpC  ../res/scripts utnso@$vm_4:/home/utnso/lissandra/res

read -p "Done..."
