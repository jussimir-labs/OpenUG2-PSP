#!/bin/sh
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here" || exit 1
if [ ! -x ./nfsu2-generator ]; then
 echo "Download and extract the complete native macOS package from the beta release."
 exit 1
fi
# Browsers quarantine downloads, and macOS would ask again for the helper, the
# included Python and each library. Opening this launcher once is the approval.
/usr/bin/xattr -dr com.apple.quarantine . 2>/dev/null || true
exec ./nfsu2-generator serve
