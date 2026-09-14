#pragma once

/*
 * "Return to Launcher" exit path for SolarOS running as a Launcher-installed
 * app (OTA app-partition slot) on shared ESP32-S3 hardware.
 *
 * CORRECTED 2026-09-01: an earlier version of this comment (and the .c
 * file) claimed Launcher's own slot on D1L uses the "factory" app-partition
 * subtype, generalized from a single grep hit against a different board's
 * (16MB P4) partition CSV. The real D1L partition tables
 * (support_files/custom_8Mb.csv and custom_8Mb2.csv, checked directly)
 * both show:
 *     app0, app, test, 0x10000, 0x150000,
 * i.e. subtype "test" (ESP_PARTITION_SUBTYPE_APP_TEST), not "factory".
 * The implementation now checks TEST first, FACTORY as a fallback for
 * portability to other Launcher-supported boards. If you're reading this
 * on a board other than D1L, verify against that board's own CSV rather
 * than trusting either subtype by default.
 *
 * Confirmed via the real Launcher source: every
 * `launcherPartitionSetOtaBoot()`/`launcherPartitionClearOtaBoot()` call
 * site is Launcher acting on itself during install/app-switch — nothing
 * exposes a "call this to return" hook to installed apps. That's fine:
 * the app-partition subtype is a fixed ESP-IDF enum value, so any app can
 * find its way back using the stock `esp_ota_ops.h` API directly, with no
 * dependency on Launcher's internal partition-table model at all.
 */

#include <stdbool.h>

/* Returns true if a factory-subtype app partition was found (implies
 * Launcher, or some other factory-slot firmware, is present to return to).
 * Does not reboot — call solar_os_launcher_return_now() to actually jump. */
bool solar_os_launcher_return_available(void);

/* Sets the boot partition to the factory slot and reboots immediately.
 * Does not return on success. Logs and returns false if no factory
 * partition exists (e.g. this build was flashed standalone, not through
 * Launcher) so the caller can fall back to a normal reboot instead. */
bool solar_os_launcher_return_now(void);
