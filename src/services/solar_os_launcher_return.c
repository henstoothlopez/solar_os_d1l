#include "solar_os_launcher_return.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static const char *TAG = "launcher_return";

/*
 * CORRECTED 2026-09-01: the original version of this file only checked
 * ESP_PARTITION_SUBTYPE_APP_FACTORY, generalized from a single data point
 * (a 16MB P4 board's partition CSV, found by accident in a broader grep).
 * The real SenseCAP D1L partition tables (support_files/custom_8Mb.csv
 * and custom_8Mb2.csv, both uploaded and checked directly) use:
 *
 *   app0, app, test, 0x10000, 0x150000,   (or 0x160000 in the *2 variant)
 *
 * i.e. subtype "test" (ESP_PARTITION_SUBTYPE_APP_TEST), NOT "factory".
 * Neither D1L CSV pre-declares separate ota_0/ota_1 slots either — just
 * this one app0 partition plus nvs/otadata/phy_init/coredump. Launcher's
 * own installer (partition_install_layout.cpp) carves OTA app partitions
 * out of whatever flash space is left over at install time, rather than
 * reading them from a fixed layout — consistent with the
 * launcherPartitionFreeRanges()/launcherPartitionFindFreeRange() grep
 * hits from the return-to-launcher investigation.
 *
 * This function now checks TEST first (matches D1L, and is what actually
 * matters for this port), then falls back to FACTORY (matches the 16MB P4
 * board and possibly other Launcher targets), so the same SolarOS binary
 * isn't hardcoded to one board's convention if this code ever gets reused
 * elsewhere.
 */
static const esp_partition_t *find_launcher_partition(void)
{
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, NULL);
    if (p != NULL) {
        return p;
    }
    return esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
}

bool solar_os_launcher_return_available(void)
{
    return find_launcher_partition() != NULL;
}

bool solar_os_launcher_return_now(void)
{
    const esp_partition_t *launcher = find_launcher_partition();
    if (launcher == NULL) {
        ESP_LOGW(TAG, "No test- or factory-subtype app partition found — "
                       "this build was likely flashed standalone, not "
                       "through Launcher. Nothing to return to.");
        return false;
    }

    ESP_LOGI(TAG, "Returning to Launcher partition '%s' (subtype 0x%02x, offset 0x%06lx)",
             launcher->label, launcher->subtype, (unsigned long)launcher->address);

    esp_err_t err = esp_ota_set_boot_partition(launcher);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    esp_restart(); /* does not return */
    return true;   /* unreachable, keeps the compiler happy */
}
