#!/usr/bin/env bash
# Generuje czcionke 6x10 z polskimi literami dla U8g2 (tylko potrzebne znaki: ASCII + ą ć ę ł ń ó ś ź ż
# i wielkie). Wymaga narzedzia bdfconv i pliku 6x10.bdf z repozytorium olikraus/u8g2:
#   git clone --depth 1 --filter=blob:none --sparse https://github.com/olikraus/u8g2
#   cd u8g2 && git sparse-checkout set --skip-checks tools/font/bdfconv tools/font/bdf/6x10.bdf
#   make -C tools/font/bdfconv
#   U8G2_REPO=$PWD tools/czcionka/generuj.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
R=${U8G2_REPO:?ustaw U8G2_REPO=sciezka do sklonowanego olikraus/u8g2}
OUT="$ROOT/firmware/sterownik_oleju/czcionka_pl.h"
TMP=$(mktemp)
"$R/tools/font/bdfconv/bdfconv" -f 1 -m '32-126,211,243,260-263,280,281,321-324,346,347,377-380' \
  -n u8g2_font_6x10_pl -o "$TMP" "$R/tools/font/bdf/6x10.bdf"
{
  echo "// Czcionka 6x10 (Misc Fixed, domena publiczna) z polskimi literami - wygenerowana przez"
  echo "// tools/czcionka/generuj.sh. Rysowac funkcjami drawUTF8 / getUTF8Width."
  echo "#pragma once"
  echo "#include <U8g2lib.h>"
  cat "$TMP"
} > "$OUT"
rm -f "$TMP"
echo "zapisano $OUT"
