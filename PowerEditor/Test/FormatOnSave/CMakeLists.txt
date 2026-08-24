# Copyright (C)2026 Don HO <don.h@free.fr>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# at your option any later version.

cmake_minimum_required(VERSION 3.20)
project(PowerEditorFormatOnSaveTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(POWEREDITOR_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src")

add_executable(powereditor_format_on_save_tests
	FormatOnSaveTests.cpp
	"${POWEREDITOR_SOURCE_DIR}/FormatOnSave.cpp"
	"${POWEREDITOR_SOURCE_DIR}/pugixml/pugixml.cpp")

target_include_directories(powereditor_format_on_save_tests PRIVATE
	"${POWEREDITOR_SOURCE_DIR}"
	"${POWEREDITOR_SOURCE_DIR}/json"
	"${POWEREDITOR_SOURCE_DIR}/pugixml")

target_compile_features(powereditor_format_on_save_tests PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(powereditor_format_on_save_tests PRIVATE /W4 /permissive-)
else()
	target_compile_options(powereditor_format_on_save_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME powereditor_format_on_save_tests COMMAND powereditor_format_on_save_tests)
