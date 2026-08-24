# This file is part of Notepad++ project
# Copyright (C)2025 Don HO <don.h@free.fr>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# at your option any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.20)
project(PowerEditorAiCoreOfflineTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(POWEREDITOR_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src")
set(AI_SOURCE_DIR "${POWEREDITOR_SOURCE_DIR}/AI")

add_executable(powereditor_ai_core_tests
	AiCoreTests.cpp
	"${AI_SOURCE_DIR}/AiCore.cpp"
	"${AI_SOURCE_DIR}/AiProvider.cpp"
	"${AI_SOURCE_DIR}/AiSecretStore.cpp"
	"${AI_SOURCE_DIR}/AiWinHttpTransport.cpp"
	"${POWEREDITOR_SOURCE_DIR}/MISC/sha2/sha-256.cpp")

target_include_directories(powereditor_ai_core_tests PRIVATE
	"${POWEREDITOR_SOURCE_DIR}"
	"${POWEREDITOR_SOURCE_DIR}/MISC/sha2"
	"${POWEREDITOR_SOURCE_DIR}/json")

target_compile_features(powereditor_ai_core_tests PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(powereditor_ai_core_tests PRIVATE /W4 /permissive-)
else()
	target_compile_options(powereditor_ai_core_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
target_link_libraries(powereditor_ai_core_tests PRIVATE Threads::Threads)

if(WIN32)
	target_compile_definitions(powereditor_ai_core_tests PRIVATE NOMINMAX)
	target_link_libraries(powereditor_ai_core_tests PRIVATE winhttp advapi32)
endif()

enable_testing()
add_test(NAME powereditor_ai_core_tests COMMAND powereditor_ai_core_tests)
