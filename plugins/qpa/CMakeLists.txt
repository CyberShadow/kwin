include_directories(${Qt5Core_PRIVATE_INCLUDE_DIRS})
include_directories(${Qt5Gui_PRIVATE_INCLUDE_DIRS})
add_definitions(-DQ_FONTCONFIGDATABASE)

set(QPA_SOURCES
    abstractplatformcontext.cpp
    backingstore.cpp
    integration.cpp
    main.cpp
    nativeinterface.cpp
    platformcontextwayland.cpp
    screen.cpp
    sharingplatformcontext.cpp
    window.cpp
)

add_library(KWinQpaPlugin MODULE ${QPA_SOURCES})
target_link_libraries(KWinQpaPlugin
    kwin
    KF5::WaylandClient
    Qt5PlatformSupport::Qt5PlatformSupport
    ${FONTCONFIG_LIBRARIES}
    ${FREETYPE_LIBRARIES}
)

if(HAVE_WAYLAND_EGL)
    target_link_libraries(KWinQpaPlugin Wayland::Egl)
endif()

install(
    TARGETS
        KWinQpaPlugin
    DESTINATION
        ${PLUGIN_INSTALL_DIR}/platforms/
)
