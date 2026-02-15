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
find_package(ZLIB REQUIRED)

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
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
  src/io/providers/google_earth_nodedata_client.cpp
  src/io/providers/rocktree_node_data_parser.cpp
  src/io/providers/planetoid_metadata_parser.cpp
  src/io/providers/bulk_metadata_parser.cpp
  src/io/providers/ge_rate_limiter.cpp
  src/io/providers/rocktree_octree_index.cpp
  src/io/decoded_tile_blob.cpp
  src/io/tile_cache.cpp
  src/io/memory_tile_cache.cpp
  src/io/tile_url_template.cpp
  src/io/ge_mesh_url_template.cpp
  src/io/dem_manager.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
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
  src/rendering/rockmesh_manager.cpp
  src/rendering/rockmesh_runtime.cpp
  src/rendering/pbo_upload_manager.cpp
  src/rendering/texture_array_manager.cpp
  src/io/node_data_cache.cpp
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
  ZLIB::ZLIB
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

# Terrain RGB Decoder No-Data Clamp Test (GE Parity Stabilization Aşama 1.1)
add_executable(terrain_rgb_decoder_nodata_test
  tests/terrain_rgb_decoder_nodata_test.cpp
)
target_include_directories(terrain_rgb_decoder_nodata_test PRIVATE
  src
)
target_link_libraries(terrain_rgb_decoder_nodata_test PRIVATE glm::glm)
add_test(NAME TerrainRGBDecoderNoDataTest COMMAND terrain_rgb_decoder_nodata_test)

# Tile State Fallback Stability Test (GE Parity Stabilization Aşama 3.3)
add_executable(tile_state_fallback_stability_test
  tests/tile_state_fallback_stability_test.cpp
  src/scheduling/tile_state_machine.cpp
)
target_include_directories(tile_state_fallback_stability_test PRIVATE
  src
)
target_link_libraries(tile_state_fallback_stability_test PRIVATE glm::glm)
add_test(NAME TileStateFallbackStabilityTest COMMAND tile_state_fallback_stability_test)

# Pipeline Integration Smoke Test (GE Parity Stabilization Aşama 4.2)
add_executable(pipeline_integration_smoke_test
  tests/pipeline_integration_smoke_test.cpp
  src/scheduling/tile_state_machine.cpp
)
target_include_directories(pipeline_integration_smoke_test PRIVATE
  src
)
target_link_libraries(pipeline_integration_smoke_test PRIVATE glm::glm)
add_test(NAME PipelineIntegrationSmokeTest COMMAND pipeline_integration_smoke_test)

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
set_tests_properties(DepthPrecisionTest PROPERTIES LABELS "known-failing")

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

# Memory cache pinning test (P0/P1 kapanış)
add_executable(memory_cache_pinning_test
  tests/memory_cache_pinning_test.cpp
  src/io/memory_tile_cache.cpp
)
target_include_directories(memory_cache_pinning_test PRIVATE
  src
)
target_link_libraries(memory_cache_pinning_test PRIVATE glm::glm)
add_test(NAME MemoryCachePinningTest COMMAND memory_cache_pinning_test)

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
  src/io/stb_image_impl.c
  src/io/decoded_tile_blob.cpp
)
target_include_directories(tile_decoder_blob_fastpath_test PRIVATE
  src
  third_party
)
target_link_libraries(tile_decoder_blob_fastpath_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME TileDecoderBlobFastpathTest COMMAND tile_decoder_blob_fastpath_test)

# TileFetcher cancel -> re-request regression test
add_executable(tile_fetcher_cancel_rerequest_test
  tests/tile_fetcher_cancel_rerequest_test.cpp
  src/io/tile_fetcher.cpp
  src/io/decoded_tile_blob.cpp
  src/io/providers/dem_fetch_result.cpp
)
target_include_directories(tile_fetcher_cancel_rerequest_test PRIVATE
  src
)
target_link_libraries(tile_fetcher_cancel_rerequest_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME TileFetcherCancelRerequestTest COMMAND tile_fetcher_cancel_rerequest_test)

# Tile state machine cancel transition test
add_executable(tile_state_machine_cancel_test
  tests/tile_state_machine_cancel_test.cpp
  src/scheduling/tile_state_machine.cpp
)
target_include_directories(tile_state_machine_cancel_test PRIVATE
  src
)
target_link_libraries(tile_state_machine_cancel_test PRIVATE glm::glm)
add_test(NAME TileStateMachineCancelTest COMMAND tile_state_machine_cancel_test)

# Tile scheduler cancel flow integration test
add_executable(tile_scheduler_cancel_flow_test
  tests/tile_scheduler_cancel_flow_test.cpp
  src/scheduling/tile_scheduler.cpp
  src/scheduling/tile_state_machine.cpp
  src/io/tile_fetcher.cpp
  src/io/tile_decoder.cpp
  src/io/stb_image_impl.c
  src/io/decoded_tile_blob.cpp
  src/io/tile_cache.cpp
  src/io/memory_tile_cache.cpp
  src/io/tile_url_template.cpp
)
target_include_directories(tile_scheduler_cancel_flow_test PRIVATE
  src
  third_party
)
target_link_libraries(tile_scheduler_cancel_flow_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME TileSchedulerCancelFlowTest COMMAND tile_scheduler_cancel_flow_test)

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
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
)
target_include_directories(dem_fallback_test PRIVATE
  src
)
target_link_libraries(dem_fallback_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
add_test(NAME DemFallbackTest COMMAND dem_fallback_test)

# DEM continuity test (exact + ancestor mixed path)
add_executable(dem_continuity_test
  tests/dem_continuity_test.cpp
  src/io/dem_manager.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
)
target_include_directories(dem_continuity_test PRIVATE
  src
)
target_link_libraries(dem_continuity_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
add_test(NAME DemContinuityTest COMMAND dem_continuity_test)

# DEM edge equalization fallback regression test
add_executable(dem_edge_equalization_fallback_test
  tests/dem_edge_equalization_fallback_test.cpp
  src/rendering/tile_mesh_builder.cpp
  src/rendering/mesh_template.cpp
  src/core/ellipsoid.cpp
  src/io/dem_manager.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
)
target_include_directories(dem_edge_equalization_fallback_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(dem_edge_equalization_fallback_test PRIVATE glm::glm glad CURL::libcurl ZLIB::ZLIB)
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
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
)
target_include_directories(selective_skirt_mask_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(selective_skirt_mask_test PRIVATE glm::glm glad CURL::libcurl ZLIB::ZLIB)
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
  src/io/dem_manager.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
  src/core/ellipsoid.cpp
  tests/stubs/network_panel_stub.cpp
)
target_include_directories(tile_pyramid_child_ready_test PRIVATE
  src
  third_party/glad/include
)
target_link_libraries(tile_pyramid_child_ready_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
add_test(NAME TilePyramidChildReadyTest COMMAND tile_pyramid_child_ready_test)

# DEM effective-level policy test
add_executable(dem_effective_level_policy_test
  tests/dem_effective_level_policy_test.cpp
  src/io/dem_manager.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
)
target_include_directories(dem_effective_level_policy_test PRIVATE
  src
)
target_link_libraries(dem_effective_level_policy_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
add_test(NAME DemEffectiveLevelPolicyTest COMMAND dem_effective_level_policy_test)

# DEM grid row-order test (south->north service ordering)
add_executable(dem_grid_row_order_test
  tests/dem_grid_row_order_test.cpp
  src/io/dem_manager.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
)
target_include_directories(dem_grid_row_order_test PRIVATE
  src
)
target_link_libraries(dem_grid_row_order_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
add_test(NAME DemGridRowOrderTest COMMAND dem_grid_row_order_test)

# DEM/raster co-eviction behavior test
add_executable(dem_co_eviction_test
  tests/dem_co_eviction_test.cpp
  src/io/dem_manager.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
)
target_include_directories(dem_co_eviction_test PRIVATE
  src
)
target_link_libraries(dem_co_eviction_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
add_test(NAME DemCoEvictionTest COMMAND dem_co_eviction_test)

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
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
)
target_include_directories(seam_metric_truth_test PRIVATE
  src
)
target_link_libraries(seam_metric_truth_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
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

# Auth backoff reset behavior test
add_executable(auth_backoff_reset_test
  tests/auth_backoff_reset_test.cpp
  src/io/dem_manager.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
)
target_include_directories(auth_backoff_reset_test PRIVATE
  src
)
target_compile_definitions(auth_backoff_reset_test PRIVATE NATIVE_GLOBE_TESTING)
target_link_libraries(auth_backoff_reset_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
add_test(NAME AuthBackoffResetTest COMMAND auth_backoff_reset_test)

# Decode metadata preservation test
add_executable(decode_metadata_preserve_test
  tests/decode_metadata_preserve_test.cpp
  src/io/providers/dem_fetch_result.cpp
)
target_include_directories(decode_metadata_preserve_test PRIVATE
  src
)
target_link_libraries(decode_metadata_preserve_test PRIVATE glm::glm)
add_test(NAME DecodeMetadataPreserveTest COMMAND decode_metadata_preserve_test)

# Google Earth Elevation Provider unit test (network-isolated)
add_executable(ge_elevation_provider_test
  tests/ge_elevation_provider_test.cpp
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/dem_fetch_result.cpp
)
target_include_directories(ge_elevation_provider_test PRIVATE
  src
)
target_link_libraries(ge_elevation_provider_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
add_test(NAME GeElevationProviderTest COMMAND ge_elevation_provider_test)

# Google Earth DEM Grid Order test
add_executable(ge_dem_grid_order_test
  tests/ge_dem_grid_order_test.cpp
  tests/stubs/network_panel_stub.cpp
  src/io/providers/google_earth_dem_provider.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/dem_manager.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/core/ellipsoid.cpp
)
target_include_directories(ge_dem_grid_order_test PRIVATE
  src
)
target_link_libraries(ge_dem_grid_order_test PRIVATE glm::glm CURL::libcurl ZLIB::ZLIB)
add_test(NAME GeDemGridOrderTest COMMAND ge_dem_grid_order_test)

# TileKey QuadKey conversion test (Track B: #9)
add_executable(tile_key_quadkey_test
  tests/tile_key_quadkey_test.cpp
)
target_include_directories(tile_key_quadkey_test PRIVATE
  src
)
target_link_libraries(tile_key_quadkey_test PRIVATE glm::glm)
add_test(NAME TileKeyQuadKeyTest COMMAND tile_key_quadkey_test)

# GE Mesh URL template test (Track B: #9)
add_executable(ge_mesh_url_template_test
  tests/ge_mesh_url_template_test.cpp
  src/io/ge_mesh_url_template.cpp
)
target_include_directories(ge_mesh_url_template_test PRIVATE
  src
)
target_link_libraries(ge_mesh_url_template_test PRIVATE glm::glm)
add_test(NAME GeMeshUrlTemplateTest COMMAND ge_mesh_url_template_test)

# GE NodeData client test (Phase 5 Sprint 1)
add_executable(ge_nodedata_client_test
  tests/ge_nodedata_client_test.cpp
  src/io/providers/google_earth_nodedata_client.cpp
  src/io/providers/http_transport.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/ge_mesh_url_template.cpp
  tests/stubs/network_panel_stub.cpp
)
target_include_directories(ge_nodedata_client_test PRIVATE
  src
  ${CURL_INCLUDE_DIRS}
)
target_link_libraries(ge_nodedata_client_test PRIVATE 
  glm::glm
  CURL::libcurl
)
add_test(NAME GeNodeDataClientTest COMMAND ge_nodedata_client_test)

# GE NodeData parser test (Phase 5 Sprint 1)
add_executable(ge_nodedata_parser_test
  tests/ge_nodedata_parser_test.cpp
  src/io/providers/rocktree_node_data_parser.cpp
)
target_include_directories(ge_nodedata_parser_test PRIVATE
  src
)
target_link_libraries(ge_nodedata_parser_test PRIVATE glm::glm)
add_test(NAME GeNodeDataParserTest COMMAND ge_nodedata_parser_test)

# RockMesh Manager test (Phase 5 Sprint 2)
add_executable(rockmesh_manager_test
  tests/rockmesh_manager_test.cpp
  src/rendering/rockmesh_manager.cpp
  src/rendering/rockmesh_runtime.cpp
  src/io/providers/google_earth_nodedata_client.cpp
  src/io/providers/rocktree_node_data_parser.cpp
  src/io/providers/planetoid_metadata_parser.cpp
  src/io/providers/bulk_metadata_parser.cpp
  src/io/providers/ge_rate_limiter.cpp
  src/io/providers/rocktree_octree_index.cpp
  src/io/ge_mesh_url_template.cpp
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/node_data_cache.cpp
  src/io/stb_image_impl.c
  tests/stubs/network_panel_stub.cpp
)
target_include_directories(rockmesh_manager_test PRIVATE
  src
  third_party
  ${CURL_INCLUDE_DIRS}
)
target_link_libraries(rockmesh_manager_test PRIVATE 
  glm::glm
  glad
  glfw
  CURL::libcurl
)
add_test(NAME RockMeshManagerTest COMMAND rockmesh_manager_test)

# ============================================================================
# GE Parity Stabilization Tests (Faz 1)
# ============================================================================

# Reversed-Z precision test
add_executable(reversed_z_precision_test
  tests/reversed_z_precision_test.cpp
  src/camera/earth_camera.cpp
  src/core/ellipsoid.cpp
)
target_include_directories(reversed_z_precision_test PRIVATE
  src
)
target_link_libraries(reversed_z_precision_test PRIVATE glm::glm)
add_test(NAME ReversedZPrecisionTest COMMAND reversed_z_precision_test)
set_tests_properties(ReversedZPrecisionTest PROPERTIES LABELS "known-failing")

# RTE/RTC Tile regression test (standalone, no GL/DEM dependencies)
add_executable(rte_rtc_tile_regression_test
  tests/rte_rtc_tile_regression_test.cpp
)
target_include_directories(rte_rtc_tile_regression_test PRIVATE
  src
)
target_link_libraries(rte_rtc_tile_regression_test PRIVATE glm::glm)
add_test(NAME RteRtcTileRegressionTest COMMAND rte_rtc_tile_regression_test)

# RTE/RTC RockMesh regression test
add_executable(rte_rtc_rockmesh_regression_test
  tests/rte_rtc_rockmesh_regression_test.cpp
)
target_include_directories(rte_rtc_rockmesh_regression_test PRIVATE
  src
)
target_link_libraries(rte_rtc_rockmesh_regression_test PRIVATE glm::glm)
add_test(NAME RteRtcRockmeshRegressionTest COMMAND rte_rtc_rockmesh_regression_test)
set_tests_properties(RteRtcRockmeshRegressionTest PROPERTIES LABELS "known-failing")

# Shader uniform parity test
add_executable(shader_uniform_parity_test
  tests/shader_uniform_parity_test.cpp
)
target_include_directories(shader_uniform_parity_test PRIVATE
  src
)
target_link_libraries(shader_uniform_parity_test PRIVATE glm::glm)
add_test(NAME ShaderUniformParityTest COMMAND shader_uniform_parity_test)

# PBO Upload Manager test (Faz 2A)
add_executable(pbo_upload_manager_test
  tests/pbo_upload_manager_test.cpp
  src/rendering/pbo_upload_manager.cpp
)
target_include_directories(pbo_upload_manager_test PRIVATE
  src
  _deps/glad-src/include
)
target_link_libraries(pbo_upload_manager_test PRIVATE glad glm::glm)
if(APPLE)
  target_link_libraries(pbo_upload_manager_test PRIVATE "-framework OpenGL")
elseif(WIN32)
  target_link_libraries(pbo_upload_manager_test PRIVATE opengl32)
else()
  target_link_libraries(pbo_upload_manager_test PRIVATE GL)
endif()
add_test(NAME PboUploadManagerTest COMMAND pbo_upload_manager_test)

# PBO + TextureManager integration test
add_executable(pbo_texture_manager_integration_test
  tests/pbo_texture_manager_integration_test.cpp
  src/rendering/pbo_upload_manager.cpp
)
target_include_directories(pbo_texture_manager_integration_test PRIVATE
  src
  _deps/glad-src/include
)
target_link_libraries(pbo_texture_manager_integration_test PRIVATE glad glm::glm)
if(APPLE)
  target_link_libraries(pbo_texture_manager_integration_test PRIVATE "-framework OpenGL")
elseif(WIN32)
  target_link_libraries(pbo_texture_manager_integration_test PRIVATE opengl32)
else()
  target_link_libraries(pbo_texture_manager_integration_test PRIVATE GL)
endif()
add_test(NAME PboTextureManagerIntegrationTest COMMAND pbo_texture_manager_integration_test)

# TextureArrayManager test (Faz 2B)
add_executable(texture_array_manager_test
  tests/texture_array_manager_test.cpp
  src/rendering/texture_array_manager.cpp
)
target_include_directories(texture_array_manager_test PRIVATE
  src
  _deps/glad-src/include
)
target_link_libraries(texture_array_manager_test PRIVATE glad glm::glm)
if(APPLE)
  target_link_libraries(texture_array_manager_test PRIVATE "-framework OpenGL")
elseif(WIN32)
  target_link_libraries(texture_array_manager_test PRIVATE opengl32)
else()
  target_link_libraries(texture_array_manager_test PRIVATE GL)
endif()
add_test(NAME TextureArrayManagerTest COMMAND texture_array_manager_test)

# RockTree Octree mapping determinism test (Faz 2B closure)
add_executable(rocktree_octree_mapping_test
  tests/rocktree_octree_mapping_test.cpp
  src/io/providers/rocktree_octree_index.cpp
  src/io/providers/planetoid_metadata_parser.cpp
  src/io/providers/bulk_metadata_parser.cpp
  src/io/providers/ge_rate_limiter.cpp
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/dem_fetch_result.cpp
)
target_include_directories(rocktree_octree_mapping_test PRIVATE
  src
)
target_compile_definitions(rocktree_octree_mapping_test PRIVATE NATIVE_GLOBE_TESTING)
target_link_libraries(rocktree_octree_mapping_test PRIVATE glm::glm CURL::libcurl)
add_test(NAME RockTreeOctreeMappingTest COMMAND rocktree_octree_mapping_test)

# Tile crossfade + array-unpop compatibility test
add_executable(tile_crossfade_array_unpop_test
  tests/tile_crossfade_array_unpop_test.cpp
)
target_include_directories(tile_crossfade_array_unpop_test PRIVATE
  src
)
target_link_libraries(tile_crossfade_array_unpop_test PRIVATE glm::glm)
add_test(NAME TileCrossfadeArrayUnpopTest COMMAND tile_crossfade_array_unpop_test)

# Horizon Culler test (Faz 3)
add_executable(horizon_culler_test
  tests/horizon_culler_test.cpp
)
target_include_directories(horizon_culler_test PRIVATE
  src
  _deps/glm-src
)
target_link_libraries(horizon_culler_test PRIVATE glm::glm)
add_test(NAME HorizonCullerTest COMMAND horizon_culler_test)

# BoundedQueue test (core data structure for RockMesh scheduling)
add_executable(bounded_queue_test
  tests/bounded_queue_test.cpp
)
target_include_directories(bounded_queue_test PRIVATE
  src
)
target_link_libraries(bounded_queue_test PRIVATE glm::glm)
add_test(NAME BoundedQueueTest COMMAND bounded_queue_test)

# NodeData disk cache test (Sprint 2.2)
add_executable(node_data_cache_test
  tests/node_data_cache_test.cpp
  src/io/node_data_cache.cpp
)
target_include_directories(node_data_cache_test PRIVATE
  src
)
target_link_libraries(node_data_cache_test PRIVATE glm::glm)
add_test(NAME NodeDataCacheTest COMMAND node_data_cache_test)

# FAZ 6 KAPANIŞ: Bayer Matrix Dithering Test (Fix 3)
add_executable(bayer_matrix_dithering_test
  tests/bayer_matrix_dithering_test.cpp
)
target_include_directories(bayer_matrix_dithering_test PRIVATE
  src
  third_party
)
target_link_libraries(bayer_matrix_dithering_test PRIVATE glm::glm)
add_test(NAME BayerMatrixDitheringTest COMMAND bayer_matrix_dithering_test)

# FAZ 6 KAPANIŞ: Instanced Array Rendering Test (Fix 1)
add_executable(instanced_array_rendering_test
  tests/instanced_array_rendering_test.cpp
  src/rendering/tile_renderer.cpp
  src/rendering/shader_manager.cpp
  src/rendering/pbo_upload_manager.cpp
  src/rendering/texture_array_manager.cpp
  src/rendering/texture_manager.cpp
  src/io/memory_tile_cache.cpp
  src/io/tile_cache.cpp
  src/io/dem_manager.cpp
  src/io/providers/dem_fetch_result.cpp
  src/io/providers/terrain_rgb_provider.cpp
  src/io/terrain_rgb_decoder.cpp
  src/io/stb_image_impl.c
  src/io/providers/http_transport.cpp
  src/io/providers/protobuf_wire.cpp
  src/io/providers/google_earth_elevation_provider.cpp
  src/io/providers/google_earth_dem_provider.cpp
  src/core/ellipsoid.cpp
  src/scheduling/tile_pyramid.cpp
  src/scheduling/lod_selector.cpp
)
target_include_directories(instanced_array_rendering_test PRIVATE
  src
  third_party
)
target_link_libraries(instanced_array_rendering_test PRIVATE glm::glm glad imgui glfw CURL::libcurl ZLIB::ZLIB)
add_test(NAME InstancedArrayRenderingTest COMMAND instanced_array_rendering_test)

# FAZ 6 KAPANIŞ: PBO Callback Integration Test (Fix 2)
add_executable(pbo_callback_integration_test
  tests/pbo_callback_integration_test.cpp
)
target_include_directories(pbo_callback_integration_test PRIVATE
  src
  third_party
)
target_link_libraries(pbo_callback_integration_test PRIVATE glm::glm)
add_test(NAME PboCallbackIntegrationTest COMMAND pbo_callback_integration_test)
