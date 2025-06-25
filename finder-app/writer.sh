#!/bin/bash

writefile=$1
writestr=$2
if [ $# -lt 2 ]
then
 echo "Error: No Arguments"
 exit 1
else
 touch $writefile
 if [ ! -f $writefile ] 
 then
 echo "Error: File could not be created"
 exit 1
 else
 echo $writestr > $writefile 
 fi
fi

