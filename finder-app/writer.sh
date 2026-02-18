#!/bin/sh

if [ $# -lt 2 ]
then
    echo "Error: two arguments required: <writefile> <writestr>"
    exit 1
fi

writefile="$1"
writestr="$2"

# using mkdir to create the directory
# -p gives no error if existing, make parent directories as needed according to man.
mkdir -p "$(dirname "$writefile")" 
if [ $? -ne 0 ]
then
    echo "Error: could not create directory path for '$writefile'"
    exit 1
fi

# using printf to write the string to the file
printf "%s" "$writestr" > "$writefile"
if [ $? -ne 0 ]
then
    echo "Error: could not write to tue '$writefile' file"
    exit 1
fi