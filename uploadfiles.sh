#!/bin/sh
for file in `ls -A1 $1`; do curl -F "file=@$1/$file" esp-eval3.fritz.box/upload; done

