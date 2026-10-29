#!/bin/sh
# Linux: starts the local helper and opens the generator in the browser.
# From a file manager it opens a terminal window for the helper's messages.
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here" || exit 1
if [ "$(uname -s)" != Linux ] || [ ! -f ./nfsu2-generator ]; then
 echo "Download and extract the complete native Linux package from the beta release."
 exit 1
fi
# Some archive tools drop the execute permission: restore it on the package's
# own programs (only inside this folder).
if [ ! -x ./nfsu2-generator ]; then
 chmod u+x ./nfsu2-generator runtime/bin/* runtime/python/bin/* 2>/dev/null
fi
if [ ! -t 1 ] && [ -z "${NFSU2_TERMINAL:-}" ] && [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
 NFSU2_TERMINAL=1; export NFSU2_TERMINAL
 self="$here/$(basename -- "$0")"
 for t in konsole gnome-terminal xfce4-terminal x-terminal-emulator xterm; do
  command -v "$t" >/dev/null 2>&1 || continue
  # A terminal that cannot start falls through to running here.
  case $t in
   gnome-terminal) "$t" -- sh "$self" "$@" && exit 0 ;;
   xfce4-terminal) "$t" -x sh "$self" "$@" && exit 0 ;;
   *) "$t" -e sh "$self" "$@" && exit 0 ;;
  esac
  break
 done
fi
./nfsu2-generator serve "$@"
status=$?
if [ "$status" -ne 0 ] && [ -n "${NFSU2_TERMINAL:-}" ]; then
 printf 'Press Enter to close this window. '
 read -r _
fi
exit "$status"
