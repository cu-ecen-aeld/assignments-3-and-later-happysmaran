#!/bin/sh

if [ $# -eq 2 ]; then
    if [ -d $1 ]; then
        x=$(ls $1 -r | wc -l)
	y=$(grep -r $2 $1 | wc -l)
	echo "The number of files are $x and the number of matching lines are $y."
	exit 0
    else
	echo "The specified directory does not exist. Double check your inputs."
    fi
    exit 1

else
    echo "Failed to process - did you enter correct number of args? (2)"
    exit 1

fi
