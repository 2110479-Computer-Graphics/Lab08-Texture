#!/bin/sh
set -e
COMMIT=f0569113c93ad095470c54bf34a17b36646bbbb5
BASE=https://raw.githubusercontent.com/nothings/stb/$COMMIT
DIR=$(dirname "$0")/stb
mkdir -p "$DIR"
curl -fL -o "$DIR/stb_image.h"       "$BASE/stb_image.h"
curl -fL -o "$DIR/stb_image_write.h" "$BASE/stb_image_write.h"
