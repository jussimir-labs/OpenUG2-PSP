#!/bin/sh
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here" || exit 1
if [ ! -x ./nfsu2-generator ]; then
 echo "Download and extract the complete native macOS package from the beta release."
 exit 1
fi
exec ./nfsu2-generator serve
