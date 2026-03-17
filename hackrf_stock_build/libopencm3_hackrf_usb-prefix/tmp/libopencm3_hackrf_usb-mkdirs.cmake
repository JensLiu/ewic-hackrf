# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/jens/Documents/academic/n3cat/hackrf/firmware/libopencm3")
  file(MAKE_DIRECTORY "/home/jens/Documents/academic/n3cat/hackrf/firmware/libopencm3")
endif()
file(MAKE_DIRECTORY
  "/home/jens/Documents/academic/n3cat/hackrf/firmware/hackrf_usb/build/libopencm3_hackrf_usb-prefix/src/libopencm3_hackrf_usb-build"
  "/home/jens/Documents/academic/n3cat/hackrf/firmware/hackrf_usb/build/libopencm3_hackrf_usb-prefix"
  "/home/jens/Documents/academic/n3cat/hackrf/firmware/hackrf_usb/build/libopencm3_hackrf_usb-prefix/tmp"
  "/home/jens/Documents/academic/n3cat/hackrf/firmware/hackrf_usb/build/libopencm3_hackrf_usb-prefix/src/libopencm3_hackrf_usb-stamp"
  "/home/jens/Documents/academic/n3cat/hackrf/firmware/hackrf_usb/build/libopencm3_hackrf_usb-prefix/src"
  "/home/jens/Documents/academic/n3cat/hackrf/firmware/hackrf_usb/build/libopencm3_hackrf_usb-prefix/src/libopencm3_hackrf_usb-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/jens/Documents/academic/n3cat/hackrf/firmware/hackrf_usb/build/libopencm3_hackrf_usb-prefix/src/libopencm3_hackrf_usb-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/jens/Documents/academic/n3cat/hackrf/firmware/hackrf_usb/build/libopencm3_hackrf_usb-prefix/src/libopencm3_hackrf_usb-stamp${cfgdir}") # cfgdir has leading slash
endif()
