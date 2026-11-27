# Torus Glide supplies createPluginFilter() for the shared editor lifecycle test.
add_executable(hba_windows_editor_tests
    WindowsEditorLifetimeTests.cpp
    WindowsGlyphLifetimeTests.cpp)
target_link_libraries(hba_windows_editor_tests PRIVATE
    TorusGlide tg_warnings visage comctl32 ole32 Catch2::Catch2WithMain)
catch_discover_tests(hba_windows_editor_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS windows-editor)
