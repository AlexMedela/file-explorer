#!/bin/sh
# file-explorer as the system file chooser: xdg-desktop-portal-termfilechooser runs this for every app's
# Open / Save / Choose folder dialog. Arguments (see xdg-desktop-portal-termfilechooser(5)):
#   $1 multiple  $2 directory  $3 save  $4 suggested path  $5 output file  $6 debug
# The app writes the chosen path(s) to the output file, one per line; cancelling leaves it empty.
multiple="$1"
directory="$2"
save="$3"
path="$4"
out="$5"
[ "$6" = 1 ] && set -x

if [ "$save" = 1 ]; then mode=save
elif [ "$directory" = 1 ]; then mode=dir
elif [ "$multiple" = 1 ]; then mode=files
else mode=file
fi
exec "${FE_BIN:-file-explorer}" --pick "$mode" --out "$out" "$path"
