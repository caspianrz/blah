cmake_minimum_required(VERSION 4.0.0)
project(blah)

# C++ version
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(blah
	src/blah_app.cpp
	src/blah_filesystem.cpp
	src/blah_common.cpp
	src/blah_time.cpp
	src/blah_input.cpp
	src/blah_stream.cpp
	src/blah_graphics.cpp
	src/blah_string.cpp
	src/blah_batch.cpp
	src/blah_spritefont.cpp
	src/blah_subtexture.cpp
	src/blah_aseprite.cpp
	src/blah_audio.cpp
	src/blah_font.cpp
	src/blah_image.cpp
	src/blah_packer.cpp
	src/blah_audio.cpp
	src/internal/blah_renderer_opengl.cpp
	src/internal/blah_renderer_d3d11.cpp
	src/internal/blah_platform.cpp
)

target_include_directories(blah
	PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	PRIVATE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# Platform Variables
option(BLAH_RENDERER_OPENGL "Make OpenGL Renderer available" ON)
if (WIN32)
	option(BLAH_RENDERER_D3D11 "Make D3D11 Renderer available" ON)
endif()
option(BLAH_NO_FUNCTIONAL "Don't use std::function" OFF)
option(BLAH_NO_SHARED_PTR "Don't use std::shared_ptr for Resources" OFF)
option(BLAH_NO_THREADING "Don't use threading" OFF)

# tracks which libraries we need to link, depends on Options above
set(LIBS "")

# use the OpenGL Renderer Backend
if (BLAH_RENDERER_OPENGL)
	add_compile_definitions(BLAH_RENDERER_OPENGL)
endif()

# use the D3D11 Renderer Backend
if (BLAH_RENDERER_D3D11)
	add_compile_definitions(BLAH_RENDERER_D3D11)
	set(LIBS ${LIBS} d3d11.lib dxguid.lib D3Dcompiler.lib)
endif()

# Emscripten can import SDL2 directly
if (EMSCRIPTEN)
	
	set_target_properties(blah PROPERTIES COMPILE_FLAGS "-s USE_SDL=3")
	target_link_libraries(blah "-s USE_SDL=3 -s USE_WEBGL2=1")

# Pull SDL3 from its Github repo
else()
	if (NOT DEFINED BLAH_SDL3_LIBS)
		include(FetchContent)
		set(FETCHCONTENT_QUIET FALSE)

		FetchContent_Declare(
			SDL3
			GIT_REPOSITORY https://github.com/libsdl-org/SDL
			GIT_TAG release-3.4.18
			GIT_PROGRESS TRUE
		)
		set(SDL_STATIC ON CACHE BOOL "" FORCE)
		FetchContent_MakeAvailable(SDL3)
		set(BLAH_SDL3_LIBS SDL3::SDL3)
		set(BLAH_SDL3_INCLUDE ${sdl3_SOURCE_DIRS}/include)
	endif()

	# statically link SDL3 since we're building it ourselves
	set(LIBS ${LIBS} ${BLAH_SDL3_LIBS})
	target_include_directories(blah PRIVATE ${BLAH_SDL3_INCLUDE})
endif()

target_link_libraries(blah PRIVATE ${LIBS})

# toggle options
if (BLAH_NO_FUNCTIONAL)
	add_compile_definitions(BLAH_NO_FUNCTIONAL)
endif()
if (BLAH_NO_SHARED_PTR)
	add_compile_definitions(BLAH_NO_SHARED_PTR)
endif()
if (BLAH_NO_THREADING)
	add_compile_definitions(BLAH_NO_THREADING)
endif()

