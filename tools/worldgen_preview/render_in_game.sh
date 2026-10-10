#!/bin/sh
# Renders a generated frontier world with the game's own graphics.
# Usage: render_in_game.sh DATA_DIR SETTINGS OUT_PREFIX
#   DATA_DIR    Caesar 3 data directory (screenshots are saved there by the game)
#   SETTINGS    e.g. seed=42,template=coastal,size=160,climate=0
#   OUT_PREFIX  writes OUT_PREFIX_full.png (whole map, 100% zoom) and OUT_PREFIX_minimap.png
set -e
data_dir=$1
settings=$2
out_prefix=$3
game=$(dirname "$0")/../../build/augustus

"$game" --windowed --worldgen-render "$settings" "$data_dir" > /dev/null 2>&1
full=$(ls -t "$data_dir"/full\ city\ *.png | head -n 1)
minimap=$(ls -t "$data_dir"/minimap\ *.png | head -n 1)
mv "$full" "${out_prefix}_full.png"
mv "$minimap" "${out_prefix}_minimap.png"
echo "${out_prefix}_full.png"
