# Included right after upstream's project(cuVSLAM) via CMAKE_PROJECT_cuVSLAM_INCLUDE (set by
# mayhem/build.sh). Defers including the fuzz harness targets to the end of the top-level
# CMakeLists so they can link upstream's own targets (cuvslam, edex, ...).
# (DEFER arguments are expanded at call time, so bake the path in now via EVAL.)
set(_mayhem_fuzz_targets "${CMAKE_CURRENT_LIST_DIR}/fuzz/targets.cmake")
cmake_language(EVAL CODE "cmake_language(DEFER CALL include [[${_mayhem_fuzz_targets}]])")
