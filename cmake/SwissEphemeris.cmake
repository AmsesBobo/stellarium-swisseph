# SPDX-License-Identifier: GPL-2.0-or-later
# Swiss Ephemeris integration, 2026-10-23.
# Explicitly opt-in: this combined build includes AGPLv3 software.
# Keep the upstream source and data outside Stellarium's source distribution.
if(NOT EXISTS "${SWISSEPH_SOURCE_DIR}/sweph.c")
    message(FATAL_ERROR "ENABLE_SWISSEPH requires SWISSEPH_SOURCE_DIR (see doc/swisseph-prototype.md)")
endif()
set(swe_sources swecl.c swedate.c swehel.c swehouse.c swejpl.c swemmoon.c swemplan.c sweph.c swephlib.c)
list(TRANSFORM swe_sources PREPEND "${SWISSEPH_SOURCE_DIR}/")
# Swiss normally gives SE_EPHE_PATH precedence over the explicit API argument.
# Disable only that lookup in a build-local copy; never mutate the process-wide
# environment (Qt and other libraries may read it on other threads).
file(READ "${SWISSEPH_SOURCE_DIR}/sweph.c" sweph_source)
set(sweph_environment_lookup "getenv(\"SE_EPHE_PATH\")")
string(REGEX MATCHALL "getenv\\(\"SE_EPHE_PATH\"\\)" sweph_environment_matches "${sweph_source}")
list(LENGTH sweph_environment_matches sweph_environment_count)
if(NOT sweph_environment_count EQUAL 1)
    message(FATAL_ERROR "Swiss source changed: review the explicit ephemeris path patch")
endif()
string(REPLACE "${sweph_environment_lookup}" "NULL /* Stellarium owns the explicit ephemeris path */" sweph_source "${sweph_source}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SWISSEPH_SOURCE_DIR}/sweph.c")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/swisseph/sweph.c" CONTENT "${sweph_source}" @ONLY)
list(REMOVE_ITEM swe_sources "${SWISSEPH_SOURCE_DIR}/sweph.c")
list(APPEND swe_sources "${CMAKE_CURRENT_BINARY_DIR}/swisseph/sweph.c")
add_library(stellarium_swe STATIC ${swe_sources})
set_target_properties(stellarium_swe PROPERTIES AUTOMOC OFF AUTOUIC OFF C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(stellarium_swe PUBLIC "${SWISSEPH_SOURCE_DIR}")
# Use one process-wide context protected by our mutex on all platforms.
# This avoids platform-dependent TLS and per-worker initialization/file handles.
target_compile_definitions(stellarium_swe PRIVATE TLSOFF)
if(UNIX)
    target_link_libraries(stellarium_swe PUBLIC m)
endif()
message(STATUS "Experimental Swiss Ephemeris enabled: review GPLv3/AGPLv3 distribution obligations")
