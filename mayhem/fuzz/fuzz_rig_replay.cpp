// Fuzz harness for the dataset replay rig that cuvslam_api_launcher (libs/launcher) and the tracker
// tools read a recorded sequence through: camera_rig_edex::CameraRigEdex. start() parses the .edex
// rig, the frame-metadata JSONL and the IMU JSONL it names; getFrame() then replays the events and
// loads each frame image, its "_mask" image and its depth image (TGA/PNG/NPY loaders).
//
// Input: up to six files separated by the line "\n%%FILE%%\n", written into a fresh per-input
// directory as rig.edex, frames.jsonl, imu.jsonl, img.tga, img_mask.tga, depth.npy (paths inside
// the edex/JSONL are resolved relative to it, exactly as the launcher does). The directory is
// removed after every input, so no state survives between inputs.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "camera_rig_edex/camera_rig_edex.h"

namespace {
constexpr std::string_view kSep = "\n%%FILE%%\n";
const char* const kNames[] = {"rig.edex", "frames.jsonl", "imu.jsonl", "img.tga", "img_mask.tga", "depth.npy"};
constexpr int kMaxFrames = 16;

std::string MakeTmpDir() {
  const char* d = getenv("TMPDIR");
  std::string buf = std::string(d && *d ? d : "/tmp") + "/fuzz_rig_XXXXXX";
  if (!mkdtemp(&buf[0])) abort();
  return buf;
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string dir = MakeTmpDir();
  std::string_view in(reinterpret_cast<const char*>(data), size);
  for (const char* name : kNames) {
    const size_t pos = in.find(kSep);
    const std::string_view part = in.substr(0, pos);
    FILE* f = fopen((dir + "/" + name).c_str(), "wb");
    if (!f) abort();
    if (!part.empty()) fwrite(part.data(), 1, part.size(), f);
    fclose(f);
    if (pos == std::string_view::npos) break;
    in.remove_prefix(pos + kSep.size());
  }

  try {
    cuvslam::camera_rig_edex::CameraRigEdex rig(dir + "/rig.edex");
    size_t imu_events = 0;
    rig.registerIMUCallback([&imu_events](const cuvslam::imu::ImuMeasurement&) { ++imu_events; });
    if (rig.start() == cuvslam::ErrorCode::S_True) {
      if (!rig.isFinished()) {  // the launcher reads the time range of a non-empty replay
        (void)rig.getFirstTimestamp();
        (void)rig.getLastTimestamp();
      }
      for (uint32_t i = 0; i < rig.getCamerasNum(); ++i) {
        (void)rig.getIntrinsic(i);
        (void)rig.getExtrinsic(i);
      }
      cuvslam::Sources sources, masks;
      cuvslam::Metas metas;
      cuvslam::DepthSources depths;
      for (int n = 0; n < kMaxFrames && !rig.isFinished(); ++n) {
        if (rig.getFrame(sources, metas, masks, depths) == cuvslam::ErrorCode::E_Bounds) break;
      }
      (void)rig.stop();
    }
  } catch (const std::exception&) {
    // jsoncpp throws Json::Exception on type mismatches (asInt on a string, ...), cnpy throws
    // std::runtime_error and DepthLoader std::invalid_argument on unreadable files.
  }
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  return 0;
}
