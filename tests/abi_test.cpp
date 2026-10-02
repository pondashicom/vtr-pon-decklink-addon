#include "decklink_addon_api.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
extern "C" int c_header_test(void);
#define CHECK(expression) do { if (!(expression)) { std::cerr << "Failed line " << __LINE__ << ": " #expression << '\n'; std::exit(1); } } while (false)

int wmain(int argc, wchar_t **argv) {
    CHECK(argc == 2); CHECK(c_header_test());
    const auto dll = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    CHECK(dll);
    const auto get = reinterpret_cast<VtrPonGetDeckLinkApi>(GetProcAddress(dll, "vtrpon_decklink_get_api"));
    CHECK(get && !get(0) && !get(2));
    const auto *api = get(1);
    CHECK(api && api->abiVersion == 1 && api->structSize == sizeof(*api));
    CHECK(api->flags & VtrPonDeckLinkTestOnly);
    CHECK(std::strcmp(api->addonId, "vtr-pon-decklink-test-addon") == 0);
    char error[128]{}; void *context = nullptr; uint32_t count = 99;
    CHECK(api->create(nullptr, error, sizeof(error)) == VtrPonDeckLinkInvalidArgument);
    CHECK(api->create(&context, error, sizeof(error)) == VtrPonDeckLinkOk && context);
    CHECK(api->listModes(context, 0, 1, nullptr, 0, &count, error, sizeof(error)) == VtrPonDeckLinkInvalidArgument && count == 0);
    CHECK(api->listDevices(context, nullptr, 0, &count, error, sizeof(error)) == VtrPonDeckLinkBufferTooSmall && count == 2);
    VtrPonDeckLinkDevice devices[2]{};
    devices[0].index = 123;
    CHECK(api->listDevices(context, devices, 1, &count, error, sizeof(error)) == VtrPonDeckLinkBufferTooSmall);
    CHECK(devices[0].index == 123); // Never write a partial snapshot.
    CHECK(api->listDevices(context, nullptr, 2, &count, error, sizeof(error)) == VtrPonDeckLinkInvalidArgument && count == 0);
    CHECK(api->listDevices(context, devices, 2, &count, error, sizeof(error)) == VtrPonDeckLinkOk && count == 2);
    CHECK(devices[0].directions == 3 && devices[1].directions == 1);
    VtrPonDeckLinkMode modes[2]{};
    CHECK(api->listModes(context, 0, 2, modes, 2, &count, error, sizeof(error)) == VtrPonDeckLinkOk);
    CHECK(count == 2 && modes[0].timeScale == 30000 && modes[0].frameDuration == 1001);
    CHECK(api->listModes(context, 1, 2, modes, 2, &count, error, sizeof(error)) == VtrPonDeckLinkInvalidArgument);
    CHECK(api->listModes(context, 2, 1, modes, 2, &count, error, sizeof(error)) == VtrPonDeckLinkInvalidArgument);
    CHECK(api->listModes(context, 0, 3, modes, 2, &count, error, sizeof(error)) == VtrPonDeckLinkInvalidArgument);
    std::thread wrongThread([&] {
        CHECK(api->listDevices(context, nullptr, 0, &count, error, sizeof(error)) == VtrPonDeckLinkWrongThread);
        CHECK(api->destroy(context, error, sizeof(error)) == VtrPonDeckLinkWrongThread);
    }); wrongThread.join();
    CHECK(api->listDevices(nullptr, nullptr, 0, &count, error, 1) == VtrPonDeckLinkInvalidArgument);
    CHECK(error[0] == 0); // Even a one-byte error buffer stays terminated.
    CHECK(api->destroy(context, error, sizeof(error)) == VtrPonDeckLinkOk);
    CHECK(api->destroy(nullptr, nullptr, 0) == VtrPonDeckLinkOk);
    CHECK(FreeLibrary(dll)); std::cout << "ABI contract passed.\n";
}
