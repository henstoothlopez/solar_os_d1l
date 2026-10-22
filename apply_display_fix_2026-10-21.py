#!/usr/bin/env python3
"""
apply_display_fix_2026-10-21.py

Run from inside the solar_os folder:
    cd ~/firmware/launcher/esp32apps/solar_os
    python3 apply_display_fix_2026-10-21.py

It edits src/services/solar_os_st7701_display.c in three small ways:

 1. Sets SOLAR_OS_D1L_ST7701_TEST_FRAME from 1 to 0 (stops the solid red
    screen so the real SolarOS screen can appear).
 2. Changes  .frame_formats = SOLAR_OS_DISPLAY_FORMAT_INDEX2_BIT  to  0.
    Why: solar_os_display_register_target() rejects a display that
    advertises frame formats unless preferred_stream_fps is also non-zero,
    and our display never sets that. Turning the frame (game-raster) path
    off is the simplest way to rule this out. Normal terminal/GUI drawing
    is not affected. It can be restored later with a proper fps value.
 3. Adds ONE temporary log line ("DIAG u8g2 size=...") so the next boot log
    shows the drawing-area size that the display service will compare with
    480x480.

Safety: makes a backup first, and refuses to change anything unless each
piece of text is found EXACTLY ONCE.
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

edits = [
    (
        "test-frame switch",
        r"(#define\s+SOLAR_OS_D1L_ST7701_TEST_FRAME\s+)1\b",
        r"\g<1>0",
    ),
    (
        "frame_formats",
        r"(\.frame_formats\s*=\s*)SOLAR_OS_DISPLAY_FORMAT_INDEX2_BIT,",
        r"\g<1>0, /* TEMP 2026-10-21: INDEX2 frame path off; registration "
        r"needs preferred_stream_fps != 0 when frame formats are set */",
    ),
    (
        "diagnostic log",
        r"(u8g2_SetPowerSave\(&dev->u8g2, 0\);)",
        r'\g<1>\n'
        r'    ESP_LOGI("solar_os_st7701", "DIAG u8g2 size=%ux%u buf=%p board=%ux%u",\n'
        r'             (unsigned)u8g2_GetDisplayWidth(&dev->u8g2),\n'
        r'             (unsigned)u8g2_GetDisplayHeight(&dev->u8g2),\n'
        r'             (void *)u8g2_GetBufferPtr(&dev->u8g2),\n'
        r'             (unsigned)cfg.width, (unsigned)cfg.height);',
    ),
]

if "DIAG u8g2 size" in text:
    sys.exit("Already applied (found the DIAG line). Nothing to do.")

new_text = text
for label, pattern, repl in edits:
    matches = re.findall(pattern, new_text)
    if len(matches) != 1:
        sys.exit(f"ERROR: expected to find the '{label}' text exactly once, "
                 f"found {len(matches)}. NOTHING was changed.\n"
                 "Please paste your last command output and this message.")
    new_text = re.sub(pattern, repl, new_text, count=1)

backup = pathlib.Path("src/services/solar_os_st7701_display.c.bak_2026-10-21")
shutil.copyfile(path, backup)
path.write_text(new_text)
print("Done. Backup saved as", backup)
print("Applied: test frame OFF, frame_formats=0, DIAG log line added.")
