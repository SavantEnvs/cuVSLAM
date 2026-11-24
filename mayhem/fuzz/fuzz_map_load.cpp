// Fuzz harness for loading a saved SLAM map: cuvslam::Slam::LocalizeInMap() on a map folder.
// A map folder is an LMDB environment that Slam::SaveMap() writes; LocalizeInMap() opens it
// read-only and deserializes the pose graph, its hypothesis, the landmark spatial index, the
// spatial cells, landmarks and feature descriptors from the blobs in it (libs/slam).
//
// Input: a sequence of records, each
//   u8 table (low 2 bits: 0 singletons, 1 landmarks, 2 descriptors, 3 spatial cells)
//   u64 key (little endian), u32 length (little endian, clipped to the bytes left), bytes
// The records are written with the project's own LMDB writer (LmdbSlamDatabase) into a fresh
// per-input directory, so the map bytes are exactly the fuzzed blobs; LMDB's own page format is
// not fuzzed (it trusts its file). A Slam session (sync_mode, so localization runs on this thread)
// is created per input and localizes in that folder with a guess pose, so no VPR is needed. The
// directory is removed after every input; nothing survives between inputs except the read-only
// rig description and the synthetic camera image built once.
// Seeds: mayhem/map-load/testsuite, made by mayhem/fuzz/gen_map_seed.cpp from a map that
// LocalizerAndMapper saved (the same writer SaveMap uses).
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "cuvslam2.h"
#include "slam/map/database/lmdb_slam_database.h"

namespace {
constexpr int kW = 640, kH = 480;
std::vector<uint8_t> g_pixels;

std::string MakeTmpDir() {
  const char* d = getenv("TMPDIR");
  std::string buf = std::string(d && *d ? d : "/tmp") + "/fuzz_map_XXXXXX";
  if (!mkdtemp(&buf[0])) abort();
  return buf;
}

uint64_t Le(const uint8_t* p, int n) {
  uint64_t v = 0;
  for (int i = n - 1; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}
}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  // Same deterministic 8x8-block texture as upstream's localize_in_map_test.
  g_pixels.resize(static_cast<size_t>(kW) * kH);
  uint32_t s = 7;
  std::vector<uint8_t> blocks((kW / 8) * (kH / 8));
  for (auto& b : blocks) {
    s = s * 1103515245u + 12345u;
    b = static_cast<uint8_t>(s >> 16);
  }
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) g_pixels[y * kW + x] = blocks[(y / 8) * (kW / 8) + x / 8];
  cuvslam::SetVerbosity(0);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string dir = MakeTmpDir();
  {
    cuvslam::slam::LmdbSlamDatabase db;
    if (!db.Open(dir.c_str(), cuvslam::slam::LmdbSlamDatabase::OpenMode::READ_WRITE_NEW)) abort();
    size_t off = 0;
    while (size - off >= 13) {
      const auto table = static_cast<cuvslam::slam::SlamDatabaseTable>(data[off] & 3);
      const uint64_t key = Le(data + off + 1, 8);
      size_t len = static_cast<size_t>(Le(data + off + 9, 4));
      off += 13;
      if (len > size - off) len = size - off;
      const uint8_t* blob = data + off;
      off += len;
      db.SetRecord(table, key, len, [&](cuvslam::slam::BlobWriter& w) {
        w.write(blob, len);
        return true;
      });
    }
    db.Close();
  }

  try {
    cuvslam::Rig rig;
    cuvslam::Camera cam{};
    cam.size = {kW, kH};
    cam.focal = {320.f, 320.f};
    cam.principal = {320.f, 240.f};
    cam.distortion.model = cuvslam::Distortion::Model::Pinhole;
    rig.cameras.push_back(cam);

    cuvslam::Slam::Config cfg;
    cfg.use_gpu = false;
    cfg.sync_mode = true;  // LocalizeInMap runs on this thread
    cuvslam::Slam slam(rig, {0}, cfg);

    cuvslam::Image img{};
    img.pixels = g_pixels.data();
    img.width = kW;
    img.height = kH;
    img.pitch = kW;
    img.encoding = cuvslam::ImageData::Encoding::MONO;
    img.data_type = cuvslam::ImageData::DataType::UINT8;
    img.is_gpu_mem = false;
    img.timestamp_ns = 2000;
    img.camera_index = 0;

    cuvslam::Slam::LocalizationSettings settings{};
    settings.horizontal_search_radius = 0.5f;
    settings.vertical_search_radius = 0.25f;
    settings.horizontal_step = 0.5f;
    settings.vertical_step = 0.25f;
    settings.angular_step_rads = 0.1f;
    settings.vpr_candidates = 0;
    const cuvslam::Pose guess{};
    bool done = false;
    slam.LocalizeInMap(dir, 2000, &guess, {img}, settings, nullptr,
                       [&](const cuvslam::Result<cuvslam::Pose>&) { done = true; });
    (void)done;
  } catch (const std::exception&) {
    // invalid_argument/runtime_error are the API's documented error reports
  }
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  return 0;
}
