#!/bin/sh

filesdir=$1
searchstr=$2

if [ $# -ne 2 ] || [ ! -d $1 ]
then
	echo 1
else
	echo "The number of files are $(find "$1" -type f | wc -l) and the number of matching lines are $( find "$1" -type f -exec grep -r $2 {} \; | wc -l )"
fi

