#!/bin/sh
# assignment 1 submission
# Author Kiran Phalak

set -e
set -u

if [ $# -lt 2 ]; then
	echo "Need at least 2 arguments. Exiting"
	exit 1
fi

writefile=$1
writestr=$2

if [ ! -d $(dirname $writefile) ]; then
	mkdir -p $(dirname $writefile)
fi

if ! touch $writefile; then
	echo "Failed to create $writefile "
	exit 1
fi

echo $writestr > $writefile


