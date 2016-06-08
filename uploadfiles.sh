#!/bin/sh
for file in `ls -A1`; do curl -F "file=@$PWD/$file" esp-eval3.fritz.box/upload; done

