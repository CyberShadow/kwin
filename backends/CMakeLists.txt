if(HAVE_INPUT)
    if(HAVE_DRM)
        add_subdirectory(drm)
    endif()
    add_subdirectory(fbdev)
    if(HAVE_LIBHYBRIS)
        add_subdirectory(hwcomposer)
    endif()
endif()
add_subdirectory(virtual)
add_subdirectory(wayland)
if(X11_XCB_FOUND)
    add_subdirectory(x11)
endif()
