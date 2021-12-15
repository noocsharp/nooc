#!/bin/sh

for file in test/*.fail.nooc
do
	./nooc $file out && {
		printf "test %s unexpectedly passed\n" "$file"
		exit 1
	}
done

for file in test/*.pass.nooc
do
	./nooc $file out || {
		printf "test %s failed\n" "$file"
		exit 1
	}
	chmod +x out && ./out || {
		printf "test %s failed\n" "$file"
		exit 1
	}
done
