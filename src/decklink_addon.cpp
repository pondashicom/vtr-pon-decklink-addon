#include "decklink_addon_api.h"
#include "addon_runtime_api.h"
#include "decklink_media_api.h"
#include "decklink_output_api.h"
#include <cwchar>
#include "media_contract.h"
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include "DeckLinkAPI.h"
#include "DeckLinkAPIVersion.h"
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <cmath>

namespace {
using Microsoft::WRL::ComPtr;
void message(char *out, uint32_t capacity, const char *text) noexcept {
    if (!out || !capacity) return;
    const auto n = std::min<std::size_t>(std::strlen(text), capacity - 1);
    std::memcpy(out, text, n); out[n] = 0;
}
struct Bstr { BSTR value = nullptr; ~Bstr() { SysFreeString(value); } };
void utf8(BSTR value, char *out, uint32_t capacity) noexcept {
    if (!capacity) return;
    out[0] = 0;
    if (!value) return;
    // Preserve whole UTF-8 sequences when a fixed ABI name buffer is shorter.
    int length = static_cast<int>(SysStringLen(value));
    while (length > 0) {
        const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length,
            nullptr, 0, nullptr, nullptr);
        if (size > 0 && static_cast<uint32_t>(size) < capacity) {
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, out,
                static_cast<int>(capacity - 1), nullptr, nullptr); out[size] = 0; return;
        }
        --length;
    }
}
struct Context;
void closeMedia(Context *) noexcept;
struct Context {
    std::thread::id owner = std::this_thread::get_id();
    bool comInitialized = false;
    bool snapshot = false;
    std::vector<ComPtr<IDeckLink>> devices;
    std::shared_ptr<void> inputState, outputState;
    ~Context() { closeMedia(this); devices.clear(); if (comInitialized) CoUninitialize(); }
};
int32_t check(void *context, char *error, uint32_t capacity) noexcept {
    message(error, capacity, "");
    if (!context) { message(error, capacity, "Context is required."); return VtrPonDeckLinkInvalidArgument; }
    if (static_cast<Context *>(context)->owner != std::this_thread::get_id()) {
        message(error, capacity, "Use the creating MTA worker thread."); return VtrPonDeckLinkWrongThread;
    }
    return VtrPonDeckLinkOk;
}
int32_t runtime(char *error, uint32_t capacity) noexcept {
    message(error, capacity, "");
    ComPtr<IDeckLinkAPIInformation> info;
    const auto hr = CoCreateInstance(CLSID_CDeckLinkAPIInformation, nullptr, CLSCTX_INPROC_SERVER,
        IID_IDeckLinkAPIInformation, reinterpret_cast<void **>(info.GetAddressOf()));
    if (hr == REGDB_E_CLASSNOTREG) {
        message(error, capacity, "Blackmagic Desktop Video is not installed (x64 COM runtime missing).");
        return VtrPonAddonRuntimeMissing;
    }
    if (FAILED(hr)) { message(error, capacity, "Desktop Video runtime initialization failed."); return VtrPonAddonRuntimeError; }
    LONGLONG version = 0;
    if (FAILED(info->GetInt(BMDDeckLinkAPIVersion, &version)) || version < BLACKMAGIC_DECKLINK_API_VERSION) {
        message(error, capacity, "Desktop Video API 16.0 or newer is required."); return VtrPonAddonRuntimeIncompatible;
    }
    return VtrPonAddonRuntimeReady;
}
int32_t VTRPON_DECKLINK_CALL create(void **out, char *error, uint32_t capacity) noexcept {
    message(error, capacity, "");
    if (!out) return VtrPonDeckLinkInvalidArgument;
    *out = nullptr;
    try {
        auto context = std::make_unique<Context>();
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            message(error, capacity, "DeckLink requires an MTA worker thread; COM initialization failed.");
            return VtrPonDeckLinkBackendError;
        }
        context->comInitialized = true;
        const auto status = runtime(error, capacity);
        if (status != VtrPonAddonRuntimeReady)
            return status == VtrPonAddonRuntimeMissing ? VtrPonDeckLinkRuntimeMissing : VtrPonDeckLinkBackendError;
        *out = context.release(); return VtrPonDeckLinkOk;
    } catch (...) { message(error, capacity, "DeckLink context allocation failed."); return VtrPonDeckLinkBackendError; }
}
int32_t VTRPON_DECKLINK_CALL destroy(void *context, char *error, uint32_t capacity) noexcept {
    if (!context) { message(error, capacity, ""); return VtrPonDeckLinkOk; }
    const auto status = check(context, error, capacity);
    if (status == VtrPonDeckLinkOk) delete static_cast<Context *>(context);
    return status;
}
template <class T> int32_t copy(const std::vector<T> &records, T *out, uint32_t capacity,
    uint32_t *count, char *error, uint32_t errorCapacity) noexcept {
    *count = static_cast<uint32_t>(records.size());
    if (capacity < *count) {
        message(error, errorCapacity, "Snapshot buffer is too small."); return VtrPonDeckLinkBufferTooSmall;
    }
    if (!records.empty()) std::memcpy(out, records.data(), records.size() * sizeof(T));
    return VtrPonDeckLinkOk;
}
int32_t VTRPON_DECKLINK_CALL devices(void *handle, VtrPonDeckLinkDevice *out, uint32_t capacity,
    uint32_t *count, char *error, uint32_t errorCapacity) noexcept {
    if (count) *count = 0;
    const auto status = check(handle, error, errorCapacity);
    if (status != VtrPonDeckLinkOk) return status;
    if (!count || (!out && capacity)) return VtrPonDeckLinkInvalidArgument;
    auto *context = static_cast<Context *>(handle);
    // A failed refresh invalidates old indexes rather than silently retaining them.
    context->snapshot = false; context->devices.clear();
    try {
        ComPtr<IDeckLinkIterator> iterator;
        if (FAILED(CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IDeckLinkIterator, reinterpret_cast<void **>(iterator.GetAddressOf())))) {
            message(error, errorCapacity, "DeckLink device iterator could not be created."); return VtrPonDeckLinkBackendError;
        }
        std::vector<ComPtr<IDeckLink>> snapshot; std::vector<VtrPonDeckLinkDevice> records;
        for (;;) {
            ComPtr<IDeckLink> device; const auto hr = iterator->Next(device.GetAddressOf());
            if (hr == S_FALSE) break;
            if (FAILED(hr) || !device || snapshot.size() >= 256) {
                message(error, errorCapacity, "DeckLink device enumeration failed or exceeded its limit."); return VtrPonDeckLinkBackendError;
            }
            VtrPonDeckLinkDevice record{}; record.structSize = sizeof(record);
            record.index = static_cast<uint32_t>(snapshot.size());
            Bstr name; if (SUCCEEDED(device->GetDisplayName(&name.value))) utf8(name.value, record.nameUtf8, sizeof(record.nameUtf8));
            ComPtr<IDeckLinkProfileAttributes> attributes; LONGLONG directions = 0, persistentId = 0;
            if (FAILED(device.As(&attributes)) || FAILED(attributes->GetInt(BMDDeckLinkVideoIOSupport, &directions))) {
                message(error, errorCapacity, "DeckLink profile capabilities could not be read."); return VtrPonDeckLinkBackendError;
            }
            if (directions & bmdDeviceSupportsCapture) record.directions |= VtrPonDeckLinkInput;
            if (directions & bmdDeviceSupportsPlayback) record.directions |= VtrPonDeckLinkOutput;
            if (SUCCEEDED(attributes->GetInt(BMDDeckLinkPersistentID, &persistentId))) {
                record.hasPersistentId = 1; record.persistentId = persistentId;
            }
            records.push_back(record); snapshot.push_back(std::move(device));
        }
        context->devices = std::move(snapshot); context->snapshot = true;
        return copy(records, out, capacity, count, error, errorCapacity);
    } catch (...) { message(error, errorCapacity, "DeckLink enumeration allocation failed."); return VtrPonDeckLinkBackendError; }
}
int32_t VTRPON_DECKLINK_CALL modes(void *handle, uint32_t index, uint32_t direction,
    VtrPonDeckLinkMode *out, uint32_t capacity, uint32_t *count, char *error, uint32_t errorCapacity) noexcept {
    if (count) *count = 0;
    const auto status = check(handle, error, errorCapacity);
    if (status != VtrPonDeckLinkOk) return status;
    const auto *context = static_cast<Context *>(handle);
    if (!count || (!out && capacity) || !context->snapshot || index >= context->devices.size() ||
        (direction != VtrPonDeckLinkInput && direction != VtrPonDeckLinkOutput)) return VtrPonDeckLinkInvalidArgument;
    try {
        ComPtr<IDeckLinkProfileAttributes> attributes; LONGLONG directions = 0;
        if (FAILED(context->devices[index].As(&attributes)) ||
            FAILED(attributes->GetInt(BMDDeckLinkVideoIOSupport, &directions))) {
            message(error, errorCapacity, "The device profile changed or became unavailable."); return VtrPonDeckLinkBackendError;
        }
        const auto required = direction == VtrPonDeckLinkInput ? bmdDeviceSupportsCapture : bmdDeviceSupportsPlayback;
        if (!(directions & required)) return VtrPonDeckLinkInvalidArgument;
        ComPtr<IDeckLinkDisplayModeIterator> iterator;
        HRESULT hr = E_NOINTERFACE;
        if (direction == VtrPonDeckLinkInput) {
            ComPtr<IDeckLinkInput> input;
            if (SUCCEEDED(context->devices[index].As(&input))) hr = input->GetDisplayModeIterator(iterator.GetAddressOf());
        } else {
            ComPtr<IDeckLinkOutput> output;
            if (SUCCEEDED(context->devices[index].As(&output))) hr = output->GetDisplayModeIterator(iterator.GetAddressOf());
        }
        if (FAILED(hr) || !iterator) {
            message(error, errorCapacity, "The selected device direction is unavailable."); return VtrPonDeckLinkInvalidArgument;
        }
        std::vector<VtrPonDeckLinkMode> records;
        for (;;) {
            ComPtr<IDeckLinkDisplayMode> mode; hr = iterator->Next(mode.GetAddressOf());
            if (hr == S_FALSE) break;
            if (FAILED(hr) || !mode || records.size() >= 1024) {
                message(error, errorCapacity, "Display mode enumeration failed or exceeded its limit."); return VtrPonDeckLinkBackendError;
            }
            VtrPonDeckLinkMode record{}; record.structSize = sizeof(record);
            record.modeId = static_cast<uint32_t>(mode->GetDisplayMode());
            record.width = mode->GetWidth(); record.height = mode->GetHeight();
            record.fieldDominance = static_cast<uint32_t>(mode->GetFieldDominance());
            if (FAILED(mode->GetFrameRate(&record.frameDuration, &record.timeScale)) ||
                record.frameDuration <= 0 || record.timeScale <= 0) {
                message(error, errorCapacity, "Display mode frame rate could not be read."); return VtrPonDeckLinkBackendError;
            }
            Bstr name; if (SUCCEEDED(mode->GetName(&name.value))) utf8(name.value, record.nameUtf8, sizeof(record.nameUtf8));
            records.push_back(record);
        }
        return copy(records, out, capacity, count, error, errorCapacity);
    } catch (...) { message(error, errorCapacity, "DeckLink mode allocation failed."); return VtrPonDeckLinkBackendError; }
}
#include "decklink_media.inc"
const VtrPonDeckLinkApi api{VTRPON_DECKLINK_ABI, sizeof(VtrPonDeckLinkApi), 0,
    "vtr-pon-decklink-addon", create, destroy, devices, modes};
}
const VtrPonDeckLinkMediaApi *VTRPON_DECKLINK_CALL vtrpon_decklink_get_media_api(uint32_t version) {
    return version == VTRPON_DECKLINK_MEDIA_ABI ? &mediaApi : nullptr;
}
const VtrPonDeckLinkOutputApi *VTRPON_DECKLINK_CALL vtrpon_decklink_get_output_api(uint32_t version) {
    return version == VTRPON_DECKLINK_OUTPUT_ABI ? &outputApi : nullptr;
}
const VtrPonDeckLinkApi *VTRPON_DECKLINK_CALL vtrpon_decklink_get_api(uint32_t version) {
    return version == VTRPON_DECKLINK_ABI ? &api : nullptr;
}
int32_t vtrpon_addon_runtime_status_v1(char *detail, uint32_t capacity) {
    const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) { message(detail, capacity, "Runtime check requires an MTA worker thread."); return VtrPonAddonRuntimeError; }
    const auto result = runtime(detail, capacity); CoUninitialize(); return result;
}
