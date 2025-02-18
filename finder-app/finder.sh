#!/bin/sh
# assignment 1 submission
# Author Kiran Phalak

set -e
set -u

if [ $# -lt 2 ]; then
	echo "Need at least 2 arguments. Exiting"
	exit 1
fi

filesdir=$1
searchstr=$2
if ! [ -d ${filesdir} ]; then
	echo "Path ${filesdir} does not exist."
	exit 1
fi

NUMFILES="$(ls -1 $filesdir | wc -l)"

NUMLINES="$(grep -r -F $searchstr $filesdir | wc -l)"

echo "The number of files are $NUMFILES and the number of matching lines are $NUMLINES"
