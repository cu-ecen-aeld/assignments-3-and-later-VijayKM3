#!/bin/bash

filesdir=$1
searchstr=$2
if [ $# -lt 2 ]
then
 echo "Error: No Arguments"
 exit 1
elif [ ! -d "$filesdir" ]
 then
 echo "Error: Not a Direcory"
 exit 1 
else
 Numfiles=$(find "$filesdir" -type f | wc -l) 
 Nummatch=$(grep -R "$searchstr" "$filesdir" | wc -l)
 echo The number of files are $Numfiles and the number of matching lines are $Nummatch
fi

