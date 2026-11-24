#!/usr/bin/env bash
# mayhem/build.sh — cuVSLAM (CPU-only, USE_CUDA=OFF).
#  1) build-fuzz/: upstream's CMake build of the static library + the mayhem/fuzz harness targets,
#     with $SANITIZER_FLAGS + SanCov (-fsanitize=fuzzer-no-link) + DWARF-3 on the whole project.
#  2) build-test/: upstream's own gtest suites with NORMAL flags (the test.sh oracle).
# All FetchContent dependencies resolve offline from /opt/toolchains/deps (mayhem/Dockerfile).
# Build dirs are build-*/ (covered by upstream's `build*/` .gitignore entry).
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export CC CXX
cd "${SRC:-/mayhem}"
SRCDIR=$PWD

# Some dependency builds write into their source tree (zlib renames zconf.h), so each build dir
# gets its own writable copy of the pinned sources in /opt/toolchains/deps (local copy, no network).
D=/opt/toolchains/deps
deps_for() {
  local b="$1"
  if [ ! -f "$b/_mayhem_deps/.done" ]; then
    rm -rf "$b/_mayhem_deps"; mkdir -p "$b/_mayhem_deps"
    for d in "$D"/*/; do cp -a "$d/src" "$b/_mayhem_deps/$(basename "$d")"; done
    chmod -R u+w "$b/_mayhem_deps"; touch "$b/_mayhem_deps/.done"
  fi
  D="$SRCDIR/$b/_mayhem_deps"
  DEPS=(
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
  -DFETCHCONTENT_SOURCE_DIR_EIGEN=$D/eigen-3.4.1
  -DFETCHCONTENT_SOURCE_DIR_GFLAGS=$D/gflags-2.3.0
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$D/googletest-1.17.0
  -DFETCHCONTENT_SOURCE_DIR_LIBJPEG=$D/libjpeg-turbo-3.1.3
  -DFETCHCONTENT_SOURCE_DIR_JSONCPP=$D/jsoncpp-1.9.6
  -DFETCHCONTENT_SOURCE_DIR_SPDLOG=$D/spdlog-1.17.0
  -DFETCHCONTENT_SOURCE_DIR_ZLIB=$D/zlib-1.3.1
  -DFETCHCONTENT_SOURCE_DIR_LIBPNG=$D/libpng-1.6.55
  -DFETCHCONTENT_SOURCE_DIR_CNPY=$D/cnpy-4e8810b
  -DFETCHCONTENT_SOURCE_DIR_CIRCULARBUFFER=$D/circularbuffer-cef6680
  -DFETCHCONTENT_SOURCE_DIR_DENSE_HASH_MAP=$D/dense_hash_map-74277fc
  -DFETCHCONTENT_SOURCE_DIR_YAML-CPP=$D/yaml-cpp-0.9.0
  -DFETCHCONTENT_SOURCE_DIR_LMDB=$D/lmdb-0.9.31
  -DUSE_CUDA=OFF -DUSE_RERUN=OFF -DUSE_NVTX=OFF
  )
  D=/opt/toolchains/deps
}

# SanCov only when sanitizing/fuzzing is on (an explicit empty SANITIZER_FLAGS builds plain).
COV=""; [ -n "$SANITIZER_FLAGS" ] && COV="-fsanitize=fuzzer-no-link"
FFLAGS="$SANITIZER_FLAGS $COV $DEBUG_FLAGS"
ENGINE="$LIB_FUZZING_ENGINE"; [ -n "$SANITIZER_FLAGS" ] || ENGINE="$STANDALONE_FUZZ_MAIN"

# LeakSanitizer is disabled at build time: mayhem/lsan_off.cc is linked into every fuzz and
# -standalone binary (mayhem/fuzz/targets.cmake, MAYHEM_LSAN_OFF).
# 1) sanitized fuzz build. RelWithDebInfo keeps asserts (upstream strips -DNDEBUG there); its flags
#    are overridden so no plain -g (DWARF-5) is added.
deps_for build-fuzz
cmake -S . -B build-fuzz -G Ninja "${DEPS[@]}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS="$FFLAGS" -DCMAKE_CXX_FLAGS="$FFLAGS" \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1" \
  -DCUVSLAM_BUILD_SHARED_LIB=OFF \
  -DCMAKE_PROJECT_cuVSLAM_INCLUDE="$SRCDIR/mayhem/project_include.cmake" \
  -DMAYHEM_LSAN_OFF="$SRCDIR/mayhem/lsan_off.cc" -DMAYHEM_FUZZ_ENGINE="$ENGINE" -DMAYHEM_STANDALONE_MAIN="$STANDALONE_FUZZ_MAIN"
TARGETS="fuzz_odometry_multicamera fuzz_odometry_mono fuzz_odometry_rgbd fuzz_odometry_inertial fuzz_edex_read fuzz_image_load fuzz_rig_replay fuzz_map_load"
ALL=""; for t in $TARGETS; do ALL="$ALL $t $t-standalone"; done
ninja -C build-fuzz -j"$MAYHEM_JOBS" $ALL mayhem_gen_map_seed  # the generator stays in build-fuzz/
for t in $TARGETS; do
  ln -f "build-fuzz/mayhem_bin/$t" "$SRCDIR/$t"
  ln -f "build-fuzz/mayhem_bin/$t-standalone" "$SRCDIR/$t-standalone"
done
# The sanitized objects/archives are ~5 GB of DWARF; drop them once linked to keep the image
# small. A re-run simply recompiles them (ninja notices the missing outputs).
find build-fuzz -type f \( -name '*.o' -o -name '*.a' \) -delete

# 2) upstream test suites, normal flags.
deps_for build-test
cmake -S . -B build-test -G Ninja "${DEPS[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS"
ninja -C build-test -j"$MAYHEM_JOBS" cuvslam_test edex_test
