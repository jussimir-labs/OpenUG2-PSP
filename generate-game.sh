#!/bin/sh
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here" || exit 1
exec ./nfsu2-generator serve "$@"
