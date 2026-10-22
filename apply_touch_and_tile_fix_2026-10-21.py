#!/usr/bin/env python3
"""
apply_touch_and_tile_fix_2026-10-21.py

Run from inside the solar_os folder:
    cd ~/firmware/launcher/esp32apps/solar_os
    python3 apply_touch_and_tile_fix_2026-10-21.py

Two fixes, in two files:

 A) src/drivers/ft6336.c  (touch)
    ft6336_init() rejects irq_pin = -1 ("no interrupt pin wired") with
    ESP_ERR_INVALID_ARG, even though we deliberately pass -1 for D1L.
    It also would try to configure GPIO "-1" as an input. Fix: accept -1
    for irq_pin (same as reset_pin already does) and skip the interrupt-pin
    setup when irq_pin is -1.

 B) src/services/solar_os_st7701_display.c  (garbled text)
    tile_to_rgb565() reads each u8g2 tile byte as a ROW with bit = column.
    u8g2 tiles are the other way round: each of the 8 bytes is a COLUMN and
    bit 0 is the TOP pixel. The old code therefore drew every 8x8 tile
    transposed (flipped across its diagonal), which makes text look
    garbled. Fix: pixel (row, col) = bit `row` of byte `col`.

Safety: all-or-nothing. Backups are made first, and nothing is written
unless every piece of text is found EXACTLY ONCE.
"""
import pathlib
import re
import shutil
import sys

TOUCH = pathlib.Path("src/drivers/ft6336.c")
DISPLAY = pathlib.Path("src/services/solar_os_st7701_display.c")

for p in (TOUCH, DISPLAY):
    if not p.exists():
        sys.exit(f"ERROR: {p} not found. Are you inside the solar_os folder?")

touch_text = TOUCH.read_text()
display_text = DISPLAY.read_text()

if "irq optional: -1 = not wired" in touch_text or \
   "FIXED 2026-10-21: u8g2 tiles" in display_text:
    sys.exit("Already applied. Nothing to do.")


def replace_once(text, pattern, repl, label, flags=0):
    found = re.findall(pattern, text, flags)
    if len(found) != 1:
        sys.exit(f"ERROR: expected to find '{label}' exactly once, found "
                 f"{len(found)}. NOTHING was changed.\n"
                 "Please paste this message back to me.")
    return re.sub(pattern, repl, text, count=1, flags=flags)


# ---- A) touch --------------------------------------------------------------
touch_new = replace_once(
    touch_text,
    r"(\n\s+)irq_pin < 0 \|\| irq_pin >= 64\) \{",
    r"\1(irq_pin != -1 && (irq_pin < 0 || irq_pin >= 64))) {",
    "irq_pin range check",
)
touch_new = replace_once(
    touch_new,
    r'(    const gpio_config_t input = \{.*?'
    r'ESP_RETURN_ON_ERROR\(gpio_config\(&input\), TAG, '
    r'"interrupt pin config failed"\);)',
    lambda m: ("    if (irq_pin >= 0) { /* irq optional: -1 = not wired */\n"
               + m.group(1) + "\n    }"),
    "interrupt pin setup block",
    flags=re.DOTALL,
)

# ---- B) display tile conversion -------------------------------------------
display_new = replace_once(
    display_text,
    r"uint8_t bits = tile_bits\[row\];",
    "/* FIXED 2026-10-21: u8g2 tiles are 8 COLUMN bytes, bit0 = top pixel. */",
    "tile row read",
)
display_new = replace_once(
    display_new,
    r"out\[row \* 8 \+ col\] = \(bits & \(1u << col\)\) \? fg : bg;",
    "out[row * 8 + col] = (tile_bits[col] & (1u << row)) ? fg : bg;",
    "tile pixel expression",
)

# ---- write (backups first) -------------------------------------------------
shutil.copyfile(TOUCH, TOUCH.with_name(TOUCH.name + ".bak_2026-10-21"))
shutil.copyfile(DISPLAY, DISPLAY.with_name(DISPLAY.name + ".bak_2026-10-21"))
TOUCH.write_text(touch_new)
DISPLAY.write_text(display_new)
print("Done. Backups saved next to each file as *.bak_2026-10-21")
print("A) touch: irq_pin = -1 now allowed")
print("B) display: tile conversion no longer transposed")
