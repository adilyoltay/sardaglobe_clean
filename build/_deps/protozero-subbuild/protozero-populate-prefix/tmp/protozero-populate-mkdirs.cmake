# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-src")
  file(MAKE_DIRECTORY "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-src")
endif()
file(MAKE_DIRECTORY
  "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-build"
  "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-subbuild/protozero-populate-prefix"
  "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-subbuild/protozero-populate-prefix/tmp"
  "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-subbuild/protozero-populate-prefix/src/protozero-populate-stamp"
  "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-subbuild/protozero-populate-prefix/src"
  "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-subbuild/protozero-populate-prefix/src/protozero-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-subbuild/protozero-populate-prefix/src/protozero-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/adilyoltay/Desktop/native_globe_clean/build/_deps/protozero-subbuild/protozero-populate-prefix/src/protozero-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
