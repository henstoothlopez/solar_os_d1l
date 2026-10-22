#!/usr/bin/env python3
"""
apply_diagnostics_2026-10-21.py

Run from inside the solar_os folder:
    cd ~/firmware/launcher/esp32apps/solar_os
    python3 apply_diagnostics_2026-10-21.py

Adds THREE temporary log lines to src/services/solar_os_st7701_display.c so
the next boot log explains the white background and shows which drawing
path SolarOS is using. It changes no behavior.

 1. In op_set_colors():   prints the foreground/background colors it was
                          given (RGB888) and the RGB565 values it stored.
 2. In op_present_surface(): prints ONCE the first time the INDEX8 "surface"
                          path is used (size and rotation).
 3. In the DRAW_TILE handler: prints ONCE the first time a monochrome tile
                          is drawn (tile position and count).

If neither line 2 nor line 3 appears in the log, nothing is being drawn by
SolarOS and the picture on screen comes from somewhere else.

Safety: makes a backup, and changes nothing unless each anchor text is found
EXACTLY ONCE.
"""
import pathlib
import re
import shutil
import sys

path = pathlib.Path("src/services/solar_os_st7701_display.c")
if not path.exists():
    sys.exit("ERROR: src/services/solar_os_st7701_display.c not found. "
             "Are you inside the solar_os folder?")

text = path.read_text()
if "DIAG set_colors" in text:
    sys.exit("Already applied. Nothing to do.")


def insert(text, pattern, builder, label, after=True):
    found = list(re.finditer(pattern, text, re.DOTALL))
    if len(found) != 1:
        sys.exit(f"ERROR: expected '{label}' exactly once, found {len(found)}. "
                 "NOTHING was changed. Please paste this message back to me.")
    m = found[0]
    block = builder
    if after:
        return text[:m.end()] + block + text[m.end():]
    return text[:m.start()] + block + text[m.start():]


new = text

# 1. colors
new = insert(
    new,
    r"dev->bg_rgb565 = \(uint16_t\)\(\(\(background_rgb888 >> 8\) & 0xF800\) \|"
    r"\s*\(\(background_rgb888 >> 5\) & 0x07E0\) \|"
    r"\s*\(\(background_rgb888 >> 3\) & 0x001F\)\);",
    '\n    ESP_LOGI("solar_os_st7701", "DIAG set_colors fg888=%06X bg888=%06X '
    '-> fg565=%04X bg565=%04X",\n'
    '             (unsigned)foreground_rgb888, (unsigned)background_rgb888,\n'
    '             (unsigned)dev->fg_rgb565, (unsigned)dev->bg_rgb565);',
    "op_set_colors background line",
)

# 2. surface path (insert BEFORE the pixel_count line)
new = insert(
    new,
    r"const size_t pixel_count = \(size_t\)surface->native_width \* surface->native_height;",
    '    {\n'
    '        static bool surface_logged;\n'
    '        if (!surface_logged) {\n'
    '            surface_logged = true;\n'
    '            ESP_LOGI("solar_os_st7701", "DIAG first present_surface %ux%u rot=%d",\n'
    '                     (unsigned)surface->native_width,\n'
    '                     (unsigned)surface->native_height, (int)surface->rotation);\n'
    '        }\n'
    '    }\n    ',
    "present_surface pixel_count line",
    after=False,
)

# 3. tile path (insert BEFORE the rect[64] declaration)
new = insert(
    new,
    r"uint16_t rect\[64\];",
    '{\n'
    '            static bool tile_logged;\n'
    '            if (!tile_logged) {\n'
    '                tile_logged = true;\n'
    '                ESP_LOGI("solar_os_st7701", "DIAG first DRAW_TILE x=%u y=%u cnt=%u",\n'
    '                         (unsigned)tile->x_pos, (unsigned)tile->y_pos,\n'
    '                         (unsigned)tile->cnt);\n'
    '            }\n'
    '        }\n        ',
    "DRAW_TILE rect declaration",
    after=False,
)

shutil.copyfile(path, path.with_name(path.name + ".bak2_2026-10-21"))
path.write_text(new)
print("Done. Backup saved as", path.name + ".bak2_2026-10-21")
print("Added 3 DIAG log lines (set_colors, first present_surface, first DRAW_TILE).")
