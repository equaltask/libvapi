#!/bin/sh

#
# http://astyle.sourceforge.net/astyle.html
#

top="$(dirname $0)/.."

for f in $(find $top/export  -name "*.h")   \
         $(find $top/src     -name "*.cpp") \
         $(find $top/src     -name "*.c")
  do
    dos2unix -q $f 2>/dev/null
    astyle --style=linux         \
           --indent=spaces=4     \
           --align-pointer=name  \
           --suffix=none         \
           --max-code-length=160 \
           --break-after-logical \
           -p -H -U              \
           $f | grep Formatted
  done
