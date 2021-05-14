#########################################################################
# File Name: git_init.sh
# Author: BinXu
# mail: binx@xx.com
# Created Time: 2021年05月14日 星期五 15时04分35秒
#########################################################################
#!/bin/bash

[ $# -lt 2 ] && {
	echo "pls input your email and username!"
	echo "	$0 username email"
	exit -1
}

git config --global alias.ci "commit"
git config --global alias.st "status"
git config --global user.name "$1"
git config --global user.email "$2"

git config --global --list
