#!/bin/sh

if [ $# -eq 2 ]
    then
    writefile="$1"
    writestr="$2"

    if [ -d file ]
	then
	echo "$writestr" > "$writefile"
    else
	mkdir -p "$(dirname $writefile)" && touch "$writefile"
	echo "$writestr" > "$writefile"
    fi

else
    echo "Error! Did you check number of args? (2)"
    exit 1
fi
