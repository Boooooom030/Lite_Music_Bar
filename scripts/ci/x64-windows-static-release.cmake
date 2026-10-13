set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Qt's WinRT implementation and MusicBar must use the same coroutine ABI on
# MSVC 19.4x. Qt selects its own standard; CMAKE_CXX_STANDARD alone is ignored.
if(PORT STREQUAL "qtbase")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS "-DFEATURE_cxx20=ON")
endif()

set(VCPKG_ENV_PASSTHROUGH FLUENT_QT_PERL FLUENT_QT_PYTHON)
if(DEFINED ENV{FLUENT_QT_PERL})
    set(PERL "$ENV{FLUENT_QT_PERL}")
endif()
if(DEFINED ENV{FLUENT_QT_PYTHON})
    set(PYTHON3 "$ENV{FLUENT_QT_PYTHON}")
endif()
