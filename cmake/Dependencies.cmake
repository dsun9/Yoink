include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
foreach(subsystem AUDIO CAMERA GPU RENDER JOYSTICK HAPTIC SENSOR)
  set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(SDL
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG 829a65d769d935c4852f8159e964312c0957260a)
FetchContent_MakeAvailable(SDL)

# PNG/zlib targets are shared with the forthcoming FreeType color-font build.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_STRICT ON CACHE BOOL "" FORCE)
foreach(option SAMPLES TESTS BACKEND_WIC BACKEND_IMAGEIO BACKEND_STB ANI AVIF BMP JPG JXL LBM PCX PNM QOI SVG TGA TIF WEBP XCF XPM XV)
  set(SDLIMAGE_${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_LIBPNG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF ON CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_SAVE OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF_SAVE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL_image
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
  GIT_TAG 0891fc30428b518e7d018677bd6fa95c1a50f4c6
  GIT_SUBMODULES external/libpng external/zlib)
FetchContent_MakeAvailable(SDL_image)
include(${CMAKE_CURRENT_LIST_DIR}/PatchSDLImage.cmake)
if(NOT TARGET PNG::PNG)
  add_library(PNG::PNG ALIAS png_static)
endif()
set(PLUTOSVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLUTOSVG_ENABLE_FREETYPE OFF CACHE BOOL "" FORCE)
set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(PlutoSVG
  GIT_REPOSITORY https://github.com/sammycage/plutosvg.git
  GIT_TAG fd8a080b3d0bccc41f1ae708d3785e7f08398a58
  GIT_SUBMODULES plutovg)
FetchContent_MakeAvailable(PlutoSVG)
set(EXPAT_SHARED_LIBS OFF CACHE BOOL "" FORCE)
foreach(option TOOLS EXAMPLES TESTS DOCS)
  set(EXPAT_BUILD_${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(EXPAT_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(Expat
  GIT_REPOSITORY https://github.com/libexpat/libexpat.git
  GIT_TAG 2120251ef8e5fd5f398063b6508be27796689bd9
  SOURCE_SUBDIR expat)
FetchContent_MakeAvailable(Expat)

# Color bitmap emoji require PNG + zlib in FreeType. Reuse SDL_image's pins.
set(FT_DISABLE_PNG OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB OFF CACHE BOOL "" FORCE)
set(FT_REQUIRE_PNG ON CACHE BOOL "" FORCE)
set(FT_REQUIRE_ZLIB ON CACHE BOOL "" FORCE)
set(PNG_LIBRARY png_static CACHE STRING "" FORCE)
set(PNG_PNG_INCLUDE_DIR "${sdl_image_SOURCE_DIR}/external/libpng" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIR "${sdl_image_SOURCE_DIR}/external/zlib" CACHE PATH "" FORCE)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ ON CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL_ttf
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
  GIT_TAG a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b
  GIT_SUBMODULES external/freetype external/harfbuzz)
FetchContent_MakeAvailable(SDL_ttf)
include(${CMAKE_CURRENT_LIST_DIR}/PatchSDLTTF.cmake)
target_link_libraries(freetype PRIVATE png_static zlibstatic)
target_link_libraries(SDL3_ttf-static PRIVATE png_static zlibstatic)

# Portable project manifests and ZIP containers (immutable upstream revisions).
set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YYJSON_BUILD_MISC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yyjson GIT_REPOSITORY https://github.com/ibireme/yyjson.git GIT_TAG ac8f6074e1fbc43ec496aa1404b460d08b55d7a5)
FetchContent_MakeAvailable(yyjson)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git GIT_TAG 77d0dce8627735138c51770d1799a1ef48f2117d)
FetchContent_MakeAvailable(miniz)
