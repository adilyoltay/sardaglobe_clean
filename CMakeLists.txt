cmake_minimum_required(VERSION 3.16)
project(native_globe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.9
)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 0.9.9.8
)

FetchContent_Declare(
  protozero
  GIT_REPOSITORY https://github.com/mapbox/protozero.git
  GIT_TAG v1.7.1
)

FetchContent_Declare(
  vtzero
  GIT_REPOSITORY https://github.com/mapbox/vtzero.git
  GIT_TAG v1.0.0
)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.90.4
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm)

FetchContent_GetProperties(protozero)
if (NOT protozero_POPULATED)
  FetchContent_Populate(protozero)
endif()

FetchContent_GetProperties(vtzero)
if (NOT vtzero_POPULATED)
  FetchContent_Populate(vtzero)
endif()

FetchContent_GetProperties(imgui)
if (NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_include_directories(imgui PRIVATE
  ${glfw_SOURCE_DIR}/include
)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)

find_package(CURL REQUIRED)

add_library(glad STATIC
  third_party/glad/src/glad.c
)

target_include_directories(glad PUBLIC
  third_party/glad/include
)

# New modular architecture
add_executable(native_globe
  src/main.cpp
  # Camera module (from backup - Google Earth parity)
  src/camera/earth_camera.cpp
  src/camera/flight_controller.cpp
  # IO module
  src/io/tile_fetcher.cpp
  src/io/tile_decoder.cpp
  src/io/tile_cache.cpp
  src/io/dem_manager.cpp
  # Scheduling module
  src/scheduling/tile_scheduler.cpp
  src/scheduling/lod_selector.cpp
  src/scheduling/tile_state_machine.cpp
  src/scheduling/tile_pyramid.cpp
  src/scheduling/job_system.cpp
  # Rendering module
  src/rendering/texture_manager.cpp
  src/rendering/shader_manager.cpp
  src/rendering/tile_renderer.cpp
  src/rendering/tile_mesh_builder.cpp
  # Core module (OpenGlobus port)
  src/core/ellipsoid.cpp
  src/core/layer_manager.cpp
  # Engine module
  src/engine/globe_engine.cpp
)

target_include_directories(native_globe PRIVATE
  src
  third_party
  third_party/glad/include
  ${protozero_SOURCE_DIR}/include
  ${vtzero_SOURCE_DIR}/include
)

target_compile_definitions(native_globe PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(native_globe PRIVATE
  glad
  glfw
  CURL::libcurl
  glm::glm
  imgui
)

if (APPLE)
  target_link_libraries(native_globe PRIVATE "-framework OpenGL")
elseif (WIN32)
  target_link_libraries(native_globe PRIVATE opengl32)
else()
  target_link_libraries(native_globe PRIVATE GL)
endif()

# Tests (FAZ 1.2 + FAZ 6.1)
enable_testing()

# LOD Conformance Test
add_executable(lod_conformance_test
  tests/lod_conformance_test.cpp
  src/scheduling/lod_selector.cpp
)
target_include_directories(lod_conformance_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(lod_conformance_test PRIVATE glm::glm)
add_test(NAME LodConformanceTest COMMAND lod_conformance_test)

# Edge Mask Test
add_executable(edge_mask_test
  tests/edge_mask_test.cpp
)
target_include_directories(edge_mask_test PRIVATE
  src
)
target_link_libraries(edge_mask_test PRIVATE glm::glm)
add_test(NAME EdgeMaskTest COMMAND edge_mask_test)

# Globe System Validation Test (Comprehensive GIS Test)
add_executable(globe_system_test
  tests/globe_system_test.cpp
  src/scheduling/lod_selector.cpp
  src/core/ellipsoid.cpp
)
target_include_directories(globe_system_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(globe_system_test PRIVATE glm::glm)
add_test(NAME GlobeSystemTest COMMAND globe_system_test)