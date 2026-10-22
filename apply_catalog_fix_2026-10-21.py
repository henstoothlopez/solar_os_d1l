#!/usr/bin/env python3
"""
apply_catalog_fix_2026-10-21.py

Run this from inside the solar_os folder:
    cd ~/firmware/launcher/esp32apps/solar_os
    python3 apply_catalog_fix_2026-10-21.py

What it does: in boards/expansion_drivers.toml, inside the [drivers.ft6336-d1l]
section, changes the "addr_fallback" binding from  kind = "i2c_address"
to  kind = "parameter".  (The manifest generator only gives bindings a
"role" name when they are parameters, which the touch checker needs.)

Safety: it makes a backup copy first, and it refuses to change anything
unless it finds the exact text exactly once.
"""
import pathlib
import shutil
import sys

path = pathlib.Path("boards/expansion_drivers.toml")
if not path.exists():
    sys.exit("ERROR: boards/expansion_drivers.toml not found. "
             "Are you inside the solar_os folder?")

text = path.read_text()

old = '''[[drivers.ft6336-d1l.bindings]]
key = "addr_fallback"
kind = "i2c_address"
hint = "0x38"
allowed = [0x38, 0x48]'''
new = old.replace('kind = "i2c_address"', 'kind = "parameter"')

count = text.count(old)
if count == 0 and new in text:
    sys.exit("Already applied. Nothing to do.")
if count != 1:
    sys.exit(f"ERROR: expected to find the text exactly once, found it {count} "
             "times. Nothing was changed. Please paste the output of:\n"
             "  grep -n -A5 'ft6336-d1l.bindings' boards/expansion_drivers.toml")

backup = pathlib.Path("boards/expansion_drivers.toml.bak_2026-10-21")
shutil.copyfile(path, backup)
path.write_text(text.replace(old, new))
print("Done. Backup saved as", backup)
print("Changed the addr_fallback binding to kind = \"parameter\".")
