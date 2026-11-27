# Canonical, SHA-256-pinned third-party release tarballs shared by every studio product.
# Using release tarballs instead of full Git clones eliminates ~180 MB of .git history
# (nlohmann/json shrinks from ~192 MB to ~1.1 MB) and allows one shared CPM cache across
# all products, OSes, and product version bumps.

include_guard(GLOBAL)

function(hba_add_nlohmann_json)
    CPMAddPackage(
        NAME nlohmann_json
        VERSION 3.12.0
        URL "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz"
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        SYSTEM YES
        OPTIONS
            "JSON_BuildTests OFF"
            "JSON_Install OFF"
    )
endfunction()

function(hba_add_visage)
    # Override Visage's Git clone with the identical FreeType 2.14.1 commit from its upstream mirror.
    # Visage still configures the font engine before it calls FetchContent_MakeAvailable.
    include(FetchContent)
    FetchContent_Declare(freetype
        URL "https://github.com/freetype/freetype/archive/526ec5c47b9ebccc4754c85ac0c0cdf7c85a5e9b.tar.gz"
        URL_HASH SHA256=d24f53b60034dc5c48e90d122d432a8f04a05b9e0a98c8d8e49b4a759207ce2e
        EXCLUDE_FROM_ALL
    )
    if(WIN32)
        # DXGI can return NOT_CURRENTLY_AVAILABLE in a headless host. Only S_OK owns an output.
        set(visage_platform_patch PATCHES "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/visage-1.0.0-win32-output-enumeration.patch")
    endif()
    CPMAddPackage(
        NAME visage
        VERSION 1.0.0
        ${visage_platform_patch}
        URL "https://github.com/VitalAudio/visage/archive/refs/tags/v1.0.0.tar.gz"
        URL_HASH SHA256=4ad2deb0ac069c1ac070652691d7eef8ee47961748bc3859d14f49d3bd0a7335
        SYSTEM YES
    )
    set(visage_SOURCE_DIR "${visage_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(hba_add_juce)
    CPMAddPackage(
        NAME JUCE
        VERSION 9.0.6
        URL "https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.6.tar.gz"
        URL_HASH SHA256=103e6c9d5f17f19bc7b5485a890573609d326513aad4db6b045181f4936ef424
        SYSTEM YES
    )
    set(JUCE_SOURCE_DIR "${JUCE_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(hba_add_catch2)
    CPMAddPackage(
        NAME Catch2
        VERSION 3.16.1
        URL "https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz"
        URL_HASH SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
        SYSTEM YES
    )
    set(Catch2_SOURCE_DIR "${Catch2_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
