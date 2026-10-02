#pragma once
#include <stdint.h>
/* Matches the host's optional Runtime Status v1 diagnostic export.
 * No device discovery or streaming occurs during this check. */
enum VtrPonAddonRuntimeStatus {
    VtrPonAddonRuntimeReady = 0,
    VtrPonAddonRuntimeMissing = 1,
    VtrPonAddonRuntimeIncompatible = 2,
    VtrPonAddonRuntimeError = 3
};
typedef int32_t (*VtrPonCheckAddonRuntime)(char *detailUtf8, uint32_t capacity);
#ifdef __cplusplus
extern "C" {
#endif
#ifdef VTRPON_DECKLINK_ADDON_BUILD
__declspec(dllexport)
#endif
int32_t vtrpon_addon_runtime_status_v1(char *detailUtf8, uint32_t capacity);
#ifdef __cplusplus
}
#endif
