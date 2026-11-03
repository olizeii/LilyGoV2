#!/usr/bin/env bash
# =============================================================================
#  Erzeugt die LVGL-Fonts in src/ui/fonts/ (Inter + FontAwesome-Symbole).
#
#  Voraussetzungen:
#    npm install -g lv_font_conv@1.5.3     (oder FONTCONV=/pfad/zu/lv_font_conv)
#    Inter 4.1 entpackt:  https://github.com/rsms/inter/releases/tag/v4.1
#
#  Aufruf:  tools/gen_fonts.sh <inter-ttf-ordner>
#           (Ordner mit Inter-Medium.ttf, Inter-SemiBold.ttf, InterDisplay-*.ttf)
#
#  Inter steht unter der SIL Open Font License 1.1, FontAwesome 5 Free unter
#  SIL OFL 1.1 (Fonts). Die generierten .c-Dateien dürfen daher mitgeliefert werden.
# =============================================================================
set -euo pipefail

INTER="${1:?Pfad zum Inter-TTF-Ordner angeben}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/src/ui/fonts"
FA="$ROOT/.pio/libdeps/twatch-s3/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
FONTCONV="${FONTCONV:-lv_font_conv}"

mkdir -p "$OUT"

# ASCII + Latin-1 (Umlaute, °, ·) + typografische Zeichen (– — ‘ ’ „ “ • … €)
TEXT="0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201A,0x201C,0x201D,0x201E,0x2022,0x2026,0x20AC,0x2009,0x202F"

# LVGL-Standardsymbole (LV_SYMBOL_*) – identisch zu built_in_font_gen.py
LV_SYMS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"
# Zusätzliche Icons (siehe src/ui/icons.h)
EXTRA="0xF186,0xF185,0xF3C5,0xF292,0xF70C,0xF133,0xF1BC,0xF075,0xF0E7,0xF304,0xF26C,0xF120,0xF0F3,0xF054,0xF3CD,0xF530,0xF017,0xF00D,0xF1E6,0xF001,0xF095,0xF0EB,0xF011,0xF554,0xF091,0xF06D,0xF4D7"
SYMS="$LV_SYMS,$EXTRA"

gen() {  # name ttf size range [mit Symbolen: 1/0]
    local name=$1 ttf=$2 size=$3 range=$4 syms=$5
    local args=(--bpp 4 --size "$size" --no-compress --format lvgl --lv-include lvgl.h
                --font "$INTER/$ttf" -r "$range")
    [[ $syms == 1 ]] && args+=(--font "$FA" -r "$SYMS")
    echo "-> $name ($size px)"
    "$FONTCONV" "${args[@]}" -o "$OUT/$name.c"
}

gen font_caption   Inter-Medium.ttf          15 "$TEXT" 1
gen font_body      Inter-Medium.ttf          18 "$TEXT" 1
gen font_headline  Inter-SemiBold.ttf        18 "$TEXT" 1
gen font_title     Inter-SemiBold.ttf        24 "$TEXT" 1
gen font_large     InterDisplay-SemiBold.ttf 34 "$TEXT" 0
gen font_time      InterDisplay-SemiBold.ttf 76 "0x20,0x2D,0x30-0x3A" 0
gen font_time_xl   InterDisplay-Bold.ttf     88 "0x20,0x2D,0x30-0x39" 0

# Großes Icon-Set für App-Symbole (nur FontAwesome)
echo "-> font_icon (40 px)"
"$FONTCONV" --bpp 4 --size 40 --no-compress --format lvgl --lv-include lvgl.h \
    --font "$FA" -r "$SYMS" -o "$OUT/font_icon.c"

echo "Fertig: $OUT"
