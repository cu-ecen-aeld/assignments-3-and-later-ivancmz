#!/bin/sh

if [ $# -lt 2 ]
then
    echo "Error: two arguments required: <filesdir> <searchstr>"
    exit 1
fi

filesdir="$1"
searchstr="$2"

if [ ! -d "$filesdir" ]
then
    echo "Error: '$filesdir' is not a valid directory"
    exit 1
fi
# find -type f finds all the files in the directory, the pipe sends the output to wc -l
# wc -l counts the number of lines in the output of the find command
file_count=$(find "$filesdir" -type f | wc -l)

# grep -r searches recursively for the search string in the files in the directory,
# 2>/dev/null suppresses error messages, the result is sent to wc -l to count the number lines
match_count=$(grep -r "$searchstr" "$filesdir" 2>/dev/null | wc -l)

echo "The number of files are ${file_count} and the number of matching lines are ${match_count}"