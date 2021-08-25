# libstored, distributed debuggable data stores.
# Copyright (C) 2020-2021  Jochem Rutgers
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

include(ExternalProject)
find_package(Git)

ExternalProject_Add(
	heatshrink-extern
	GIT_REPOSITORY https://github.com/atomicobject/heatshrink.git
	GIT_TAG v0.4.1
	CONFIGURE_COMMAND ""
	BUILD_COMMAND ""
	INSTALL_COMMAND ""
	UPDATE_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> ${GIT_EXECUTABLE} restore .
	LOG_CONFIGURE 0
	LOG_BUILD 0
	LOG_TEST 0
	LOG_INSTALL 0
)

ExternalProject_Get_Property(heatshrink-extern SOURCE_DIR)

# heatshrink_encoder.c and heatshrink_decoder.c are considered generated files.
# Upon a clean, they are removed. Hence the UPDATE_COMMAND to recover them.
add_library(heatshrink STATIC ${SOURCE_DIR}/heatshrink_encoder.c ${SOURCE_DIR}/heatshrink_decoder.c)
set_target_properties(heatshrink PROPERTIES PUBLIC_HEADER
	"${SOURCE_DIR}/heatshrink_common.h;${SOURCE_DIR}/heatshrink_config.h;${SOURCE_DIR}/heatshrink_encoder.h;${SOURCE_DIR}/heatshrink_decoder.h")
add_dependencies(heatshrink heatshrink-extern)

get_target_property(heatshrink_src heatshrink SOURCES)
set_source_files_properties(${heatshrink_src} PROPERTIES GENERATED 1)

if(MSVC)
	target_compile_options(heatshrink PRIVATE /W1)
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		target_compile_options(heatshrink PUBLIC /MTd)
	else()
		target_compile_options(heatshrink PUBLIC /MT)
	endif()
endif()

target_include_directories(heatshrink PUBLIC $<BUILD_INTERFACE:${SOURCE_DIR}> $<INSTALL_INTERFACE:include>)

install(TARGETS heatshrink EXPORT libstored
	ARCHIVE DESTINATION lib
	PUBLIC_HEADER DESTINATION include
)

set(Heatshrink_FOUND 1)

