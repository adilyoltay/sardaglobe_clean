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
  src/io/decoded_tile_blob.cpp
  src/io/tile_cache.cpp
  src/io/memory_tile_cache.cpp
  src/io/tile_url_template.cpp
  src/io/dem_manager.cpp
  # Scheduling module
  src/scheduling/tile_scheduler.cpp
  src/scheduling/lod_selector.cpp
  src/scheduling/tile_state_machine.cpp
  src/scheduling/tile_pyramid.cpp
  src/scheduling/job_system.cpp
  # Rendering module
  src/rendering/texture_manager.cpp
  src/rendering/texture_atlas_allocator.cpp
  src/rendering/shader_manager.cpp
  src/rendering/tile_renderer.cpp
  src/rendering/mesh_template.cpp
  src/rendering/tile_mesh_builder.cpp
  src/rendering/tile_mesh_scheduler.cpp
  src/rendering/render_frame.cpp
  src/rendering/heightmap_manager.cpp
  # Core module (OpenGlobus port)
  src/core/ellipsoid.cpp
  src/core/layer_manager.cpp
  # Engine module
  src/engine/globe_engine.cpp
  # Debug module
  src/debug/network_panel.cpp
)

target_include_directories(native_globe PRIVATE
  src
  third_party
  third_party/glad/include
  ${protozero_SOURCE_DIR}/include
  ${vtzero_SOURCE_DIR}/include
)

target_compile_definitions(native_globe PRIVATE GLFW_INCLUDE_NONE)

# Build info: embed git SHA for quick binary/version verification in the debug UI.
find_package(Git QUIET)
set(NATIVE_GLOBE_GIT_SHA "unknown")
if (GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHA_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if (GIT_SHA_OUTPUT)
    set(NATIVE_GLOBE_GIT_SHA "${GIT_SHA_OUTPUT}")
  endif()
endif()
target_compile_definitions(native_globe PRIVATE NATIVE_GLOBE_GIT_SHA="${NATIVE_GLOBE_GIT_SHA}")

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
option(NATIVE_GLOBE_ENABLE_NETWORK_TESTS
  "Enable network-dependent diagnostics in ctest"
  OFF
)

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

# Predictive prefetch selection test (velocity-aware expansion)
add_executable(predictive_prefetch_test
  tests/predictive_prefetch_test.cpp
  src/scheduling/lod_selector.cpp
)
target_include_directories(predictive_prefetch_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(predictive_prefetch_test PRIVATE glm::glm)
add_test(NAME PredictivePrefetchTest COMMAND predictive_prefetch_test)

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

# Tile Fade Test (Unpop behavior)
add_executable(tile_fade_test
  tests/tile_fade_test.cpp
)
target_include_directories(tile_fade_test PRIVATE
  src
)
target_link_libraries(tile_fade_test PRIVATE glm::glm)
add_test(NAME TileFadeTest COMMAND tile_fade_test)

# Corner LOD mapping test (uCornerLods helpers)
add_executable(corner_lod_test
  tests/corner_lod_test.cpp
)
target_include_directories(corner_lod_test PRIVATE
  src
)
target_link_libraries(corner_lod_test PRIVATE glm::glm)
add_test(NAME CornerLodTest COMMAND corner_lod_test)

# Unpop/crossfade policy + UV transform test
add_executable(unpop_crossfade_policy_test
  tests/unpop_crossfade_policy_test.cpp
)
target_include_directories(unpop_crossfade_policy_test PRIVATE
  src
)
target_link_libraries(unpop_crossfade_policy_test PRIVATE glm::glm)
add_test(NAME UnpopCrossfadePolicyTest COMMAND unpop_crossfade_policy_test)

# Tile terrain morph timing test
add_executable(tile_terrain_morph_test
  tests/tile_terrain_morph_test.cpp
)
target_include_directories(tile_terrain_morph_test PRIVATE
  src
)
target_link_libraries(tile_terrain_morph_test PRIVATE glm::glm)
add_test(NAME TileTerrainMorphTest COMMAND tile_terrain_morph_test)

# Tile cache telemetry counters test
add_executable(tile_cache_stats_test
  tests/tile_cache_stats_test.cpp
  src/io/tile_cache.cpp
)
target_include_directories(tile_cache_stats_test PRIVATE
  src
)
target_link_libraries(tile_cache_stats_test PRIVATE glm::glm)
add_test(NAME TileCacheStatsTest COMMAND tile_cache_stats_test)

# Depth precision projection test (standard vs reversed-Z)
add_executable(depth_precision_test
  tests/depth_precision_test.cpp
  src/camera/earth_camera.cpp
)
target_include_directories(depth_precision_test PRIVATE
  src
)
target_link_libraries(depth_precision_test PRIVATE glm::glm)
add_test(NAME DepthPrecisionTest COMMAND depth_precision_test)

# Memory tile cache LRU/telemetry test
add_executable(memory_tile_cache_test
  tests/memory_tile_cache_test.cpp
  src/io/memory_tile_cache.cpp
)
target_include_directories(memory_tile_cache_test PRIVATE
  src
)
target_link_libraries(memory_tile_cache_test PRIVATE glm::glm)
add_test(NAME MemoryTileCacheTest COMMAND memory_tile_cache_test)

# Decoded blob pack/unpack format test
add_executable(decoded_tile_blob_test
  tests/decoded_tile_blob_test.cpp
  src/io/decoded_tile_blob.cpp
)
target_include_directories(decoded_tile_blob_test PRIVATE
  src
)
target_link_libraries(decoded_tile_blob_test PRIVATE glm::glm)
add_test(NAME DecodedTileBlobTest COMMAND decoded_tile_blob_test)

# Decoder fast-path test for pre-decoded cache blobs
add_executable(tile_decoder_blob_fastpath_test
  tests/tile_decoder_blob_fastpath_test.cpp
  src/io/tile_decoder.cpp
  src/io/decoded_tile_blob.cpp
)
target_include_directories(tile_decoder_blob_fastpath_test PRIVATE
  src
  third_party
)
target_link_libraries(tile_decoder_blob_fastpath_test PRIVATE glm::glm)
add_test(NAME TileDecoderBlobFastpathTest COMMAND tile_decoder_blob_fastpath_test)

# TileFetcher cancel -> re-request regression test
add_executable(tile_fetcher_cancel_rerequest_test
  tests/tile_fetcher_cancel_rerequest_test.cpp
  src/io/tile_fetcher.cpp
  src/io/decoded_tile_blob.cpp
)
target_include_directories(tile_fetcher_cancel_rerequest_test PRIVATE
  src
)
target_link_libraries(tile_fetcher_cancel_rerequest_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME TileFetcherCancelRerequestTest COMMAND tile_fetcher_cancel_rerequest_test)

# Texture atlas allocator test
add_executable(texture_atlas_allocator_test
  tests/texture_atlas_allocator_test.cpp
  src/rendering/texture_atlas_allocator.cpp
)
target_include_directories(texture_atlas_allocator_test PRIVATE
  src
)
target_link_libraries(texture_atlas_allocator_test PRIVATE glm::glm)
add_test(NAME TextureAtlasAllocatorTest COMMAND texture_atlas_allocator_test)

# Frustum extraction regression test
add_executable(frustum_extract_test
  tests/frustum_extract_test.cpp
)
target_include_directories(frustum_extract_test PRIVATE
  src
)
target_link_libraries(frustum_extract_test PRIVATE glm::glm)
add_test(NAME FrustumExtractTest COMMAND frustum_extract_test)

# LOD leaf non-empty regression test
add_executable(lod_leaf_nonempty_test
  tests/lod_leaf_nonempty_test.cpp
  src/scheduling/lod_selector.cpp
)
target_include_directories(lod_leaf_nonempty_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(lod_leaf_nonempty_test PRIVATE glm::glm)
add_test(NAME LodLeafNonEmptyTest COMMAND lod_leaf_nonempty_test)

# Atlas gutter + UV inset regression test
add_executable(atlas_gutter_uv_test
  tests/atlas_gutter_uv_test.cpp
  src/rendering/texture_atlas_allocator.cpp
)
target_include_directories(atlas_gutter_uv_test PRIVATE
  src
)
target_link_libraries(atlas_gutter_uv_test PRIVATE glm::glm)
add_test(NAME AtlasGutterUvTest COMMAND atlas_gutter_uv_test)

# DEM parent fallback sampling test
add_executable(dem_fallback_test
  tests/dem_fallback_test.cpp
  src/io/dem_manager.cpp
)
target_include_directories(dem_fallback_test PRIVATE
  src
)
target_link_libraries(dem_fallback_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME DemFallbackTest COMMAND dem_fallback_test)

# DEM continuity test (exact + ancestor mixed path)
add_executable(dem_continuity_test
  tests/dem_continuity_test.cpp
  src/io/dem_manager.cpp
)
target_include_directories(dem_continuity_test PRIVATE
  src
)
target_link_libraries(dem_continuity_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME DemContinuityTest COMMAND dem_continuity_test)

# DEM edge equalization fallback regression test
add_executable(dem_edge_equalization_fallback_test
  tests/dem_edge_equalization_fallback_test.cpp
  src/rendering/tile_mesh_builder.cpp
  src/rendering/mesh_template.cpp
  src/core/ellipsoid.cpp
  src/io/dem_manager.cpp
)
target_include_directories(dem_edge_equalization_fallback_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(dem_edge_equalization_fallback_test PRIVATE glm::glm glad CURL::libcurl)
add_test(NAME DemEdgeEqualizationFallbackTest COMMAND dem_edge_equalization_fallback_test)

# Mesh template stitch/skirt mask variant test
add_executable(mesh_template_stitch_mask_test
  tests/mesh_template_stitch_mask_test.cpp
  src/rendering/mesh_template.cpp
)
target_include_directories(mesh_template_stitch_mask_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(mesh_template_stitch_mask_test PRIVATE glm::glm glad)
add_test(NAME MeshTemplateStitchMaskTest COMMAND mesh_template_stitch_mask_test)

# Selective skirt mask test
add_executable(selective_skirt_mask_test
  tests/selective_skirt_mask_test.cpp
  src/rendering/tile_mesh_builder.cpp
  src/rendering/mesh_template.cpp
  src/core/ellipsoid.cpp
  src/io/dem_manager.cpp
)
target_include_directories(selective_skirt_mask_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(selective_skirt_mask_test PRIVATE glm::glm glad CURL::libcurl)
add_test(NAME SelectiveSkirtMaskTest COMMAND selective_skirt_mask_test)

# LOD child quorum policy test
add_executable(lod_child_quorum_test
  tests/lod_child_quorum_test.cpp
  src/scheduling/lod_selector.cpp
)
target_include_directories(lod_child_quorum_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(lod_child_quorum_test PRIVATE glm::glm)
add_test(NAME LodChildQuorumTest COMMAND lod_child_quorum_test)

# LOD refine budget policy test (progressive LOD transitions)
add_executable(lod_refine_budget_test
  tests/lod_refine_budget_test.cpp
  src/scheduling/lod_selector.cpp
)
target_include_directories(lod_refine_budget_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(lod_refine_budget_test PRIVATE glm::glm)
add_test(NAME LodRefineBudgetTest COMMAND lod_refine_budget_test)

# TilePyramid child-ready policy test
add_executable(tile_pyramid_child_ready_test
  tests/tile_pyramid_child_ready_test.cpp
  src/scheduling/tile_pyramid.cpp
  src/scheduling/lod_selector.cpp
)
target_include_directories(tile_pyramid_child_ready_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(tile_pyramid_child_ready_test PRIVATE glm::glm)
add_test(NAME TilePyramidChildReadyTest COMMAND tile_pyramid_child_ready_test)

# DEM effective-level policy test
add_executable(dem_effective_level_policy_test
  tests/dem_effective_level_policy_test.cpp
  src/io/dem_manager.cpp
)
target_include_directories(dem_effective_level_policy_test PRIVATE
  src
)
target_link_libraries(dem_effective_level_policy_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME DemEffectiveLevelPolicyTest COMMAND dem_effective_level_policy_test)

# DEM batch URL ordering test (ensures C1..CN mapping matches service response order)
add_executable(dem_batch_url_order_test
  tests/dem_batch_url_order_test.cpp
  src/io/dem_manager.cpp
)
target_include_directories(dem_batch_url_order_test PRIVATE
  src
)
target_link_libraries(dem_batch_url_order_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME DemBatchUrlOrderTest COMMAND dem_batch_url_order_test)

# DEM grid row-order test (south->north service ordering)
add_executable(dem_grid_row_order_test
  tests/dem_grid_row_order_test.cpp
  src/io/dem_manager.cpp
)
target_include_directories(dem_grid_row_order_test PRIVATE
  src
)
target_link_libraries(dem_grid_row_order_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME DemGridRowOrderTest COMMAND dem_grid_row_order_test)

# Adaptive mesh segments policy test
add_executable(adaptive_mesh_segments_test
  tests/adaptive_mesh_segments_test.cpp
)
target_include_directories(adaptive_mesh_segments_test PRIVATE
  src
)
target_link_libraries(adaptive_mesh_segments_test PRIVATE glm::glm)
add_test(NAME AdaptiveMeshSegmentsTest COMMAND adaptive_mesh_segments_test)

# Seam metric truth test
add_executable(seam_metric_truth_test
  tests/seam_metric_truth_test.cpp
  src/io/dem_manager.cpp
)
target_include_directories(seam_metric_truth_test PRIVATE
  src
)
target_link_libraries(seam_metric_truth_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME SeamMetricTruthTest COMMAND seam_metric_truth_test)

# Tile server connectivity diagnostic test
add_executable(tile_server_connectivity_test
  tests/tile_server_connectivity_test.cpp
)
target_include_directories(tile_server_connectivity_test PRIVATE
  src
)
target_link_libraries(tile_server_connectivity_test PRIVATE glm::glm CURL::libcurl)
if (NATIVE_GLOBE_ENABLE_NETWORK_TESTS)
  add_test(NAME TileServerConnectivityTest COMMAND tile_server_connectivity_test)
  set_tests_properties(TileServerConnectivityTest PROPERTIES LABELS "network")
endif()

# Visual parity preset gate test
add_executable(visual_parity_presets_test
  tests/visual_parity_presets_test.cpp
)
target_include_directories(visual_parity_presets_test PRIVATE
  src
)
target_link_libraries(visual_parity_presets_test PRIVATE glm::glm)
add_test(NAME VisualParityPresetsTest COMMAND visual_parity_presets_test)
