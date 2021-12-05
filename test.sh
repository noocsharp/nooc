#!/bin/sh

for file in test/*
do
	./nooc $file out || {
		printf "test %s failed\n" "$file"
		exit 1
	}
done
