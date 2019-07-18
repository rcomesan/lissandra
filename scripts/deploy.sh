source defines.sh

"/c/Program Files/PuTTY/plink.exe" utnso@$vm_1 -m deploy-pre.sh
"/c/Program Files/PuTTY/plink.exe" utnso@$vm_2 -m deploy-pre.sh
"/c/Program Files/PuTTY/plink.exe" utnso@$vm_3 -m deploy-pre.sh
"/c/Program Files/PuTTY/plink.exe" utnso@$vm_4 -m deploy-pre.sh

scp -rpC  ../cx utnso@$vm_1:/home/utnso/lissandra 
scp -rpC  ../ker utnso@$vm_1:/home/utnso/lissandra 
scp -rpC  ../mem utnso@$vm_1:/home/utnso/lissandra 
scp -rpC  ../lfs utnso@$vm_1:/home/utnso/lissandra 
scp -rpC  ../res utnso@$vm_1:/home/utnso/lissandra 
scp -rpC  ../scripts utnso@$vm_1:/home/utnso/lissandra 

scp -rpC  ../cx utnso@$vm_2:/home/utnso/lissandra 
scp -rpC  ../ker utnso@$vm_2:/home/utnso/lissandra 
scp -rpC  ../mem utnso@$vm_2:/home/utnso/lissandra 
scp -rpC  ../lfs utnso@$vm_2:/home/utnso/lissandra 
scp -rpC  ../res utnso@$vm_2:/home/utnso/lissandra 
scp -rpC  ../scripts utnso@$vm_2:/home/utnso/lissandra 

scp -rpC  ../cx utnso@$vm_3:/home/utnso/lissandra 
scp -rpC  ../ker utnso@$vm_3:/home/utnso/lissandra 
scp -rpC  ../mem utnso@$vm_3:/home/utnso/lissandra 
scp -rpC  ../lfs utnso@$vm_3:/home/utnso/lissandra 
scp -rpC  ../res utnso@$vm_3:/home/utnso/lissandra 
scp -rpC  ../scripts utnso@$vm_3:/home/utnso/lissandra 

scp -rpC  ../cx utnso@$vm_4:/home/utnso/lissandra 
scp -rpC  ../ker utnso@$vm_4:/home/utnso/lissandra 
scp -rpC  ../mem utnso@$vm_4:/home/utnso/lissandra 
scp -rpC  ../lfs utnso@$vm_4:/home/utnso/lissandra 
scp -rpC  ../res utnso@$vm_4:/home/utnso/lissandra 
scp -rpC  ../scripts utnso@$vm_4:/home/utnso/lissandra 

"/c/Program Files/PuTTY/plink.exe" utnso@$vm_1 -m deploy-post.sh
"/c/Program Files/PuTTY/plink.exe" utnso@$vm_2 -m deploy-post.sh
"/c/Program Files/PuTTY/plink.exe" utnso@$vm_3 -m deploy-post.sh
"/c/Program Files/PuTTY/plink.exe" utnso@$vm_4 -m deploy-post.sh

read -p "Done..."
