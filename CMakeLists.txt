cmake_minimum_required(VERSION 3.25)
project(vtr_pon_decklink_addon VERSION 0.3.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This add-on requires MSVC Windows x64.")
endif()
option(VTRPON_DECKLINK_ENABLE_SDK "Build the real SDK media add-on" OFF)
set(VTRPON_DECKLINK_SDK_DIR "" CACHE PATH "DeckLink SDK directory containing Win/Include")
include(CTest)

if(CMAKE_GENERATOR MATCHES "Ninja")
    # Match the native MSVC include prefix without CMake transcoding it; the
    # Japanese prefix otherwise leaks to build logs and breaks Ninja dependencies.
    set(_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/DeckLinkShowIncludes")
    file(MAKE_DIRECTORY "${_probe_dir}")
    file(WRITE "${_probe_dir}/probe.h" "\n")
    file(WRITE "${_probe_dir}/probe.cpp" "#include \"probe.h\"\nint main(){}\n")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c probe.cpp
        WORKING_DIRECTORY "${_probe_dir}"
        OUTPUT_VARIABLE _prefix_output ERROR_VARIABLE _prefix_error
        RESULT_VARIABLE _prefix_result ENCODING NONE)
    if(_prefix_result EQUAL 0 AND _prefix_output MATCHES
        "(^|\n)([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)([A-Za-z]:\\\\|\\./|\\.\\\\|/)")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
    else()
        message(FATAL_ERROR "MSVC include dependency detection failed: ${_prefix_error}")
    endif()
endif()

# The separate distribution has no Qt/GStreamer dependency. The host will load
# its C ABI; SDK-generated declarations stay private to the real DLL below.
add_library(decklink-media-core STATIC src/media_contract.cpp src/media_contract.h)
target_include_directories(decklink-media-core PUBLIC src)
add_executable(decklink-probe src/probe.cpp)
target_include_directories(decklink-probe PRIVATE include)

if(BUILD_TESTING)
    add_library(vtr-pon-decklink-test-addon SHARED tests/mock_addon.cpp)
    target_include_directories(vtr-pon-decklink-test-addon PRIVATE include)
    target_compile_definitions(vtr-pon-decklink-test-addon PRIVATE VTRPON_DECKLINK_ADDON_BUILD)
    set_target_properties(vtr-pon-decklink-test-addon PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-addons")
    add_executable(decklink-media-test tests/media_test.cpp)
    target_link_libraries(decklink-media-test PRIVATE decklink-media-core)
    add_test(NAME decklink-media-contract COMMAND decklink-media-test)
    add_executable(decklink-abi-test tests/abi_test.cpp tests/c_header_test.c)
    target_include_directories(decklink-abi-test PRIVATE include)
    add_test(NAME decklink-abi-contract COMMAND decklink-abi-test
        "$<TARGET_FILE:vtr-pon-decklink-test-addon>")
    add_test(NAME decklink-probe-mock COMMAND decklink-probe --addon-file
        "$<TARGET_FILE:vtr-pon-decklink-test-addon>" --allow-test-addon)
    add_test(NAME decklink-probe-rejects-mock COMMAND decklink-probe --addon-file
        "$<TARGET_FILE:vtr-pon-decklink-test-addon>")
    set_tests_properties(decklink-probe-rejects-mock PROPERTIES WILL_FAIL TRUE)
    add_test(NAME decklink-probe-missing-file COMMAND decklink-probe --addon-file
        "${CMAKE_BINARY_DIR}/missing-addon.dll")
    set_tests_properties(decklink-probe-missing-file PROPERTIES WILL_FAIL TRUE)
endif()

if(VTRPON_DECKLINK_ENABLE_SDK)
    set(_sdk_include "${VTRPON_DECKLINK_SDK_DIR}/Win/Include")
    if(NOT EXISTS "${_sdk_include}/DeckLinkAPI.idl")
        message(FATAL_ERROR "Set VTRPON_DECKLINK_SDK_DIR to the official SDK root containing Win/Include/DeckLinkAPI.idl.")
    endif()
    file(STRINGS "${_sdk_include}/DeckLinkAPIVersion.h" _sdk_version
        REGEX "BLACKMAGIC_DECKLINK_API_VERSION_STRING")
    if(NOT _sdk_version MATCHES "\"16\\.0\"")
        message(FATAL_ERROR "This add-on is validated with the official DeckLink SDK 16.0; review before changing SDK version.")
    endif()
    find_program(MIDL_EXECUTABLE midl REQUIRED)
    set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(GLOB_RECURSE _idl_dependencies CONFIGURE_DEPENDS "${_sdk_include}/*.idl")
    add_custom_command(
        OUTPUT "${_generated}/DeckLinkAPI.h" "${_generated}/DeckLinkAPI_i.c"
            "${_generated}/DeckLinkAPI.tlb"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated}"
        COMMAND "${MIDL_EXECUTABLE}" /nologo /env x64 /out "${_generated}"
            /h DeckLinkAPI.h /iid DeckLinkAPI_i.c /tlb DeckLinkAPI.tlb
            "${_sdk_include}/DeckLinkAPI.idl"
        DEPENDS ${_idl_dependencies}
        WORKING_DIRECTORY "${_sdk_include}"
        VERBATIM)
    set_source_files_properties("${_generated}/DeckLinkAPI_i.c" PROPERTIES GENERATED TRUE)
    add_library(vtr-pon-decklink-addon SHARED src/decklink_addon.cpp src/decklink_media.inc
        "${_generated}/DeckLinkAPI.h" "${_generated}/DeckLinkAPI_i.c")
    target_include_directories(vtr-pon-decklink-addon PRIVATE include "${_generated}" "${_sdk_include}")
    target_compile_definitions(vtr-pon-decklink-addon PRIVATE VTRPON_DECKLINK_ADDON_BUILD)
    target_link_libraries(vtr-pon-decklink-addon PRIVATE decklink-media-core ole32 oleaut32 advapi32)
    set_target_properties(vtr-pon-decklink-addon PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/decklink-addons")
    install(TARGETS vtr-pon-decklink-addon RUNTIME DESTINATION decklink-addons)
    if(BUILD_TESTING)
        add_executable(decklink-sdk-abi-test tests/sdk_abi_test.cpp)
        target_include_directories(decklink-sdk-abi-test PRIVATE include)
        add_test(NAME decklink-sdk-abi-contract COMMAND decklink-sdk-abi-test
            "$<TARGET_FILE:vtr-pon-decklink-addon>")
    endif()
endif()

get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _targets)
    get_target_property(_type ${_target} TYPE)
    if(NOT _type STREQUAL "UTILITY")
        target_compile_definitions(${_target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(${_target} PRIVATE /utf-8 /W4)
    endif()
endforeach()
# The host does not build or install this DLL. Separate local development
# packages are assembled explicitly with scripts/package.ps1.
