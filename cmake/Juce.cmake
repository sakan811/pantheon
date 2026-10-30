# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 The Pantheon authors

# JUCE for every plugin of the repository. The version is written here and nowhere else (docs say "see cmake/Juce.cmake").
# JUCE is fetched at configure time and is not part of this repository. For an offline build, put a JUCE checkout of the
# same version in third_party/JUCE at the repository root (git-ignored); it is used instead of the download.
include_guard(GLOBAL)
include(FetchContent)

set(PANTHEON_JUCE_TAG "8.0.16-security")

if(NOT FETCHCONTENT_SOURCE_DIR_JUCE AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../third_party/JUCE/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_CURRENT_LIST_DIR}/../third_party/JUCE")
endif()

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${PANTHEON_JUCE_TAG}
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)
