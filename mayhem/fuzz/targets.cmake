# Fuzz harness targets, added (deferred, include()d into the top-level directory) by mayhem/project_include.cmake.
# Each harness is linked twice: with libFuzzer (MAYHEM_FUZZ_ENGINE) and with the run-once standalone
# driver (MAYHEM_STANDALONE_MAIN, compiled as C so its LLVMFuzzerTestOneInput ref is not mangled).
set(_lsan_off ${MAYHEM_LSAN_OFF})
add_library(mayhem_standalone_main OBJECT ${MAYHEM_STANDALONE_MAIN})
set_source_files_properties(${MAYHEM_STANDALONE_MAIN} PROPERTIES LANGUAGE C)

function(mayhem_fuzzer name src)
  cmake_parse_arguments(F "" "" "DEFS;LIBS;INCS" ${ARGN})
  foreach(variant fuzz standalone)
    if(variant STREQUAL "fuzz")
      set(tgt ${name})
      add_executable(${tgt} ${src} ${_lsan_off})
      separate_arguments(_eng UNIX_COMMAND "${MAYHEM_FUZZ_ENGINE}")
      target_link_options(${tgt} PRIVATE ${_eng})
    else()
      set(tgt ${name}-standalone)
      add_executable(${tgt} ${src} ${_lsan_off} $<TARGET_OBJECTS:mayhem_standalone_main>)
    endif()
    target_compile_definitions(${tgt} PRIVATE ${F_DEFS})
    target_include_directories(${tgt} PRIVATE ${F_INCS})
    target_link_libraries(${tgt} PRIVATE ${F_LIBS})
    set_target_properties(${tgt} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/mayhem_bin)
  endforeach()
endfunction()

set(_api_inc ${CMAKE_SOURCE_DIR}/libs/cuvslam)
mayhem_fuzzer(fuzz_odometry_multicamera ${CMAKE_CURRENT_LIST_DIR}/fuzz_odometry.cpp DEFS FUZZ_MODE=0 LIBS cuvslam INCS ${_api_inc})
mayhem_fuzzer(fuzz_odometry_mono        ${CMAKE_CURRENT_LIST_DIR}/fuzz_odometry.cpp DEFS FUZZ_MODE=1 LIBS cuvslam INCS ${_api_inc})
mayhem_fuzzer(fuzz_odometry_rgbd        ${CMAKE_CURRENT_LIST_DIR}/fuzz_odometry.cpp DEFS FUZZ_MODE=2 LIBS cuvslam INCS ${_api_inc})
mayhem_fuzzer(fuzz_odometry_inertial    ${CMAKE_CURRENT_LIST_DIR}/fuzz_odometry.cpp DEFS FUZZ_MODE=3 LIBS cuvslam INCS ${_api_inc})
mayhem_fuzzer(fuzz_edex_read            ${CMAKE_CURRENT_LIST_DIR}/fuzz_edex.cpp LIBS edex common jsoncpp::jsoncpp INCS ${CMAKE_SOURCE_DIR}/libs)
mayhem_fuzzer(fuzz_image_load           ${CMAKE_CURRENT_LIST_DIR}/fuzz_image_load.cpp LIBS utils common INCS ${CMAKE_SOURCE_DIR}/libs)
mayhem_fuzzer(fuzz_rig_replay           ${CMAKE_CURRENT_LIST_DIR}/fuzz_rig_replay.cpp LIBS camera_rig_edex utils edex camera common jsoncpp::jsoncpp INCS ${CMAKE_SOURCE_DIR}/libs)
mayhem_fuzzer(fuzz_map_load             ${CMAKE_CURRENT_LIST_DIR}/fuzz_map_load.cpp LIBS cuvslam slam lmdb::lmdb INCS ${_api_inc} ${CMAKE_SOURCE_DIR}/libs)
# Seed generator for map-load (run by hand, see gen_map_seed.cpp); built by build.sh, left in build-fuzz/.
add_executable(mayhem_gen_map_seed ${CMAKE_CURRENT_LIST_DIR}/gen_map_seed.cpp ${_lsan_off})
target_include_directories(mayhem_gen_map_seed PRIVATE ${CMAKE_SOURCE_DIR}/libs)
target_link_libraries(mayhem_gen_map_seed PRIVATE slam sof camera lmdb::lmdb)
# Two UBSan checks fire on every saved map, valid ones included, before an input byte matters, so they
# are relaxed for the `slam` library only (every other ASan/UBSan check stays on and halting):
#  - alignment: libs/slam reads each record in place from LMDB's memory map, which keeps keys and values
#    only 2-byte aligned; LmdbSlamDatabase::Cursor::Get loads keys as *reinterpret_cast<size_t*> and
#    BlobReader::read (slam/common/blob.h) loads every field as *(const T*)ptr (fine on x86).
#  - null, nonnull-attribute: BlobReader::read_std does read(&dst[0], 0) for an empty vector, i.e. a
#    reference into an empty std::vector and then memcpy(nullptr, src, 0); SaveMap writes empty vectors
#    (PoseGraph keyframes), so the first keyframe hits it. A real null dereference still faults under ASan.
target_compile_options(slam PRIVATE -fno-sanitize=alignment,null,nonnull-attribute)
# LMDB (upstream's pinned liblmdb, the `lmdb_build` target from cmake/ext/lmdb.cmake) does
# memcpy(dst, NULL, 0) in mdb_node_add when a record's value is empty: an empty Blob's data() is nullptr.
# That only happens on the write path (fuzz_map_load writes the fuzzed records to build its fixture
# before LocalizeInMap reads them) and is benign, so the same two checks are relaxed for LMDB only.
if(TARGET lmdb_build)
  target_compile_options(lmdb_build PRIVATE -fno-sanitize=null,nonnull-attribute)
endif()
