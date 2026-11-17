#!/usr/bin/env python3
"""
Erzeugt die LVGL-Fonts in src/ui/fonts/ – Inter + Ersatzschriften:
  * Inter          : Text (Latin, Griechisch, Pfeile, Hoch-/Tiefzahlen, was Inter an Mathe hat)
  * DejaVu Sans    : restliche Mathe-Zeichen (∀ ∂ ∫ √ ∞ ≠ ≤ ℝ …), die Inter nicht hat
  * Noto Emoji     : häufige Emojis (einfarbig – das Display/LVGL kann keine Farb-Emojis)
  * FontAwesome 5  : LVGL-Symbole + App-Icons (src/ui/icons.h)

Voraussetzungen:
  npx (Node.js) – lv_font_conv@1.5.3 wird automatisch geholt
  pip install fonttools
  Inter 4.1 (https://github.com/rsms/inter/releases/tag/v4.1), Ordner extras/ttf
  Noto Emoji (https://github.com/google/fonts/tree/main/ofl/notoemoji)

Aufruf:  python tools/gen_fonts.py <inter-ttf-ordner> <NotoEmoji.ttf>

Alle Schriften stehen unter der SIL Open Font License 1.1 (DejaVu: Bitstream-Vera-Lizenz),
die generierten .c-Dateien dürfen mitgeliefert werden.
"""
import subprocess
import sys
from pathlib import Path

from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "src/ui/fonts"
LVGL = ROOT / ".pio/libdeps/twatch-s3/lvgl/scripts/built_in_font"
FA = LVGL / "FontAwesome5-Solid+Brands+Regular.woff"
DEJAVU = LVGL / "DejaVuSans.ttf"


def rng(a, b):
    return list(range(a, b + 1))


# ASCII + Latin-1 + typografische Zeichen
TEXT = rng(0x20, 0x7E) + rng(0xA0, 0xFF) + [0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E,
                                           0x2022, 0x2026, 0x20AC, 0x2009, 0x202F]
# Griechisch, Pfeile, Hoch-/Tiefgestellt, Mathe & Co.
EXTRA = (
    [c for c in rng(0x391, 0x3A9) if c != 0x3A2] + rng(0x3B1, 0x3C9) + [0x3D1, 0x3D5, 0x3D6, 0x3F5]
    + rng(0x2190, 0x2195) + rng(0x21D0, 0x21D4)
    + [0x2070, 0x2071] + rng(0x2074, 0x208E)
    + [0x2030, 0x2032, 0x2033, 0x2102, 0x2103, 0x210F, 0x2113, 0x2115, 0x2116, 0x211A, 0x211D, 0x2122,
       0x2124, 0x2126, 0x2135] + rng(0x2150, 0x215E)
    + [0x2200, 0x2202, 0x2203, 0x2204, 0x2205, 0x2206, 0x2207, 0x2208, 0x2209, 0x220B, 0x220F, 0x2211,
       0x2212, 0x2213, 0x2217, 0x2218, 0x2219, 0x221A, 0x221B, 0x221D, 0x221E, 0x2220, 0x2225, 0x2227,
       0x2228, 0x2229, 0x222A, 0x222B, 0x222C, 0x222E, 0x2234, 0x2235, 0x223C, 0x2245, 0x2248, 0x2260,
       0x2261, 0x2264, 0x2265, 0x226A, 0x226B, 0x2282, 0x2283, 0x2286, 0x2287, 0x2295, 0x2297, 0x22A5,
       0x22C5, 0x2300, 0x2713, 0x2717, 0x25CF, 0x25CB, 0x25A0, 0x25A1, 0x25B2, 0x25BC, 0x25B6, 0x25C0]
)
# Häufige Emojis (Gesichter komplett + Gesten, Herzen, Alltag)
EMOJI = (
    rng(0x1F600, 0x1F64F) + rng(0x1F90C, 0x1F92F) + rng(0x1F970, 0x1F97A) + rng(0x1F493, 0x1F49F)
    + rng(0x1F446, 0x1F450) + rng(0x1F4A2, 0x1F4AF)
    + [0x2764, 0x2763, 0x1F5A4, 0x1F90D, 0x1F90E, 0x1F9E1, 0x1F48B, 0x1F440, 0x1F445, 0x1F937, 0x1F926,
       0x1F9D0, 0x270C, 0x270B, 0x261D, 0x270A, 0x1F44A, 0x1F4AA, 0x1F483, 0x1F57A,
       0x1F525, 0x2728, 0x1F389, 0x1F38A, 0x1F381, 0x1F382, 0x2705, 0x274C, 0x2757, 0x2753, 0x26A0, 0x1F6AB,
       0x1F680, 0x1F480, 0x2620, 0x1F47B, 0x1F47D, 0x1F916, 0x1F648, 0x1F649, 0x1F64A,
       0x2600, 0x2601, 0x26C5, 0x2614, 0x2744, 0x26A1, 0x1F319, 0x2B50, 0x1F31F, 0x1F308, 0x1F30D, 0x1F30A,
       0x1F3B5, 0x1F3B6, 0x1F3A7, 0x1F3A4, 0x1F3AE, 0x26BD, 0x1F3C0, 0x1F3C6, 0x1F947, 0x1F3C3, 0x1F6B6,
       0x1F355, 0x1F354, 0x1F35F, 0x1F37A, 0x1F37B, 0x1F377, 0x1F942, 0x2615, 0x1F36B, 0x1F369, 0x1F34E,
       0x1F4F1, 0x1F4BB, 0x1F4E7, 0x1F4CD, 0x1F4C5, 0x1F4CC, 0x1F4CE, 0x23F0, 0x231A, 0x23F3, 0x1F697,
       0x1F68C, 0x2708, 0x1F3E0, 0x1F3EB, 0x1F4DA, 0x270F, 0x1F4DD, 0x1F4B0, 0x1F4B8, 0x1F4B5, 0x1F512,
       0x1F513, 0x1F514, 0x1F515, 0x1F50A, 0x1F507, 0x1F4F7, 0x1F4FA, 0x1F4E2, 0x1F6A8, 0x1F4A4,
       0x1F436, 0x1F431, 0x1F98B, 0x1F984, 0x1F338, 0x1F339, 0x1F33B, 0x1F340, 0x1F332,
       0x1F195, 0x1F197, 0x1F51D, 0x2714, 0x2716, 0x2795, 0x2796, 0x27A1, 0x2B05, 0x2B06, 0x2B07,
       0x1F534, 0x1F7E2, 0x1F535, 0x1F7E1, 0x26AA, 0x26AB, 0x2665, 0x2660, 0x2663, 0x2666, 0x263A, 0x2639]
)


def cmap(path):
    return set(TTFont(str(path), fontNumber=0).getBestCmap().keys())


def fmt(cps):
    """Codepoints -> lv_font_conv-Bereiche"""
    cps = sorted(set(cps))
    out, i = [], 0
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        out.append(f"0x{cps[i]:X}" if i == j else f"0x{cps[i]:X}-0x{cps[j]:X}")
        i = j + 1
    return ",".join(out)


def symbols():
    lv = [61441, 61448, 61451, 61452, 61453, 61457, 61459, 61461, 61465, 61468, 61473, 61478, 61479, 61480,
          61502, 61507, 61512, 61515, 61516, 61517, 61521, 61522, 61523, 61524, 61543, 61544, 61550, 61552,
          61553, 61556, 61559, 61560, 61561, 61563, 61587, 61589, 61636, 61637, 61639, 61641, 61664, 61671,
          61674, 61683, 61724, 61732, 61787, 61931, 62016, 62017, 62018, 62019, 62020, 62087, 62099, 62212,
          62189, 62810, 63426, 63650]
    icons = [0xF186, 0xF185, 0xF3C5, 0xF292, 0xF70C, 0xF133, 0xF1BC, 0xF075, 0xF0E7, 0xF304, 0xF26C, 0xF120,
             0xF0F3, 0xF054, 0xF3CD, 0xF530, 0xF017, 0xF00D, 0xF1E6, 0xF001, 0xF095, 0xF0EB, 0xF011, 0xF554,
             0xF091, 0xF06D, 0xF4D7, 0xF062, 0xF060, 0xF061, 0xF0E2, 0xF2F1, 0xF11E, 0xF124, 0xF279, 0xF2F6,
             0xF249, 0xF5EB, 0xF2ED, 0xF043]
    return lv + icons


def conv(name, size, fonts, args_extra=()):
    args = ["npx", "--yes", "lv_font_conv@1.5.3", "--bpp", "4", "--size", str(size), "--no-compress",
            "--format", "lvgl", "--lv-include", "lvgl.h"]
    for path, cps in fonts:
        if cps:
            args += ["--font", str(path), "-r", fmt(cps)]
    args += list(args_extra) + ["-o", str(OUT / f"{name}.c")]
    print(f"-> {name} ({size} px)")
    subprocess.run(args, check=True, shell=(sys.platform == "win32"))


def main():
    inter_dir, emoji = Path(sys.argv[1]), Path(sys.argv[2])
    dejavu = cmap(DEJAVU)
    emoji_cov = cmap(emoji)

    def text_font(ttf, with_extras):
        inter = inter_dir / ttf
        have = cmap(inter)
        wanted = TEXT + (EXTRA if with_extras else [])
        a = [c for c in wanted if c in have]
        b = [c for c in wanted if c not in have and c in dejavu]
        e = [c for c in EMOJI if c in emoji_cov and c not in a and c not in b] if with_extras else []
        return [(inter, a), (DEJAVU, b), (emoji, e)]

    sym = [(FA, symbols())]
    conv("font_caption", 15, text_font("Inter-Medium.ttf", True) + sym)
    conv("font_body", 18, text_font("Inter-Medium.ttf", True) + sym)
    conv("font_headline", 18, text_font("Inter-SemiBold.ttf", True) + sym)
    conv("font_title", 24, text_font("Inter-SemiBold.ttf", True) + sym)
    print("Fertig. (font_large/time/icon unverändert – siehe Git-Historie: tools/gen_fonts.sh)")


if __name__ == "__main__":
    main()
