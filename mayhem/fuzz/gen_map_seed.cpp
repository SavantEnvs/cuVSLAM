// One-off seed generator for the map-load target (not a fuzz target, not shipped in the image).
// Saves a small SLAM map the way upstream's localize_in_map_test does (LocalizerAndMapper ->
// AttachToNewDatabaseSaveMapAndDetach, the writer behind Slam::SaveMap): two keyframes and 88
// landmarks with ShiTomasi descriptors on the same synthetic texture fuzz_map_load.cpp localizes
// with. Then dumps every LMDB record of the saved folder in fuzz_map_load's input format.
// Usage: gen_map_seed <scratch map folder> <output seed file>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "camera/camera.h"
#include "slam/map/database/lmdb_slam_database.h"
#include "slam/slam/slam.h"
#include "sof/image_context.h"

using namespace cuvslam;
using namespace cuvslam::slam;

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  constexpr int kW = 640, kH = 480;
  std::vector<uint8_t> pixels(kW * kH);
  uint32_t s = 7;
  std::vector<uint8_t> blocks((kW / 8) * (kH / 8));
  for (auto& b : blocks) {
    s = s * 1103515245u + 12345u;
    b = static_cast<uint8_t>(s >> 16);
  }
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) pixels[y * kW + x] = blocks[(y / 8) * (kW / 8) + x / 8];

  auto cam = camera::CreateCameraModel(Vector2T(kW, kH), Vector2T(320, 320), Vector2T(320, 240),
                                       Distortion::Model::Pinhole, nullptr, 0);
  camera::Rig rig;
  rig.intrinsics[0] = cam.get();
  rig.num_cameras = 1;
  rig.camera_from_rig[0].setIdentity();

  auto image = std::make_shared<sof::ImageContext>(ImageShape{kW, kH}, false, false);
  ImageMeta meta{};
  meta.shape = ImageShape{kW, kH};
  meta.timestamp = 1000;
  image->set_image_meta(meta);
  ImageSource src;
  src.data = pixels.data();
  src.type = ImageSource::U8;
  src.memory_type = ImageSource::Host;
  src.image_encoding = ImageEncoding::MONO8;
  if (!image->build_cpu_image_pyramid(src, false) || !image->build_cpu_gradient_pyramid(false)) return 1;

  const std::string folder = argv[1];
  std::filesystem::remove_all(folder);
  {
    LocalizerAndMapper writer(rig, FeatureDescriptorType::kShiTomasi6, false);
    writer.SetActiveCameras({0});
    writer.SetLandmarksSpatialIndex({});
    VOFrameData frame;
    frame.frame_id = 0;
    frame.timestamp_ns = 1000;
    writer.AddKeyframe(Isometry3T::Identity(), frame, sof::Images());
    Map& map = const_cast<Map&>(writer.GetMap());
    std::vector<Vector2T> uvs;
    for (int y = 40; y <= kH - 40; y += 56)
      for (int x = 40; x <= kW - 40; x += 56) uvs.emplace_back(static_cast<float>(x), static_cast<float>(y));
    std::vector<FeatureDescriptor> descriptors;
    map.feature_descriptor_ops_->CreateDescriptors(image, uvs, descriptors);
    for (size_t i = 0; i < uvs.size() && i < descriptors.size(); ++i) {
      if (!descriptors[i]) continue;
      const float depth = 3.f + static_cast<float>(i % 5);
      const Vector2T uv_norm((uvs[i].x() - 320) / 320, (uvs[i].y() - 240) / 320);
      const Vector3T xyz(uv_norm.x() * depth, uv_norm.y() * depth, depth);
      const LandmarkId id = map.landmarks_spatial_index_->AddLandmark(descriptors[i], 1000);
      map.pose_graph_.AddLandmarkRelation(id, 0);
      map.landmarks_spatial_index_->AddLandmarkRelation(id, 0, &xyz, uv_norm, map.pose_graph_hypothesis_);
    }
    Isometry3T step = Isometry3T::Identity();
    step.translation().x() = 1.f;
    frame.frame_id = 1;
    frame.timestamp_ns = 1500;
    writer.AddKeyframe(step, frame, sof::Images());
    if (!writer.AttachToNewDatabaseSaveMapAndDetach(folder)) return 1;
  }

  LmdbSlamDatabase db;
  if (!db.Open(folder.c_str(), LmdbSlamDatabase::OpenMode::READ_ONLY_EXISTS)) return 1;
  FILE* out = fopen(argv[2], "wb");
  if (!out) return 1;
  size_t n = 0;
  for (uint8_t t = 0; t < 4; ++t) {
    db.ForEach(static_cast<SlamDatabaseTable>(t), [&](ISlamDatabase::Key key, const BlobReader& r) {
      const uint32_t len = static_cast<uint32_t>(r.remaining());
      const uint8_t* p = r.feed_forward(len);
      fputc(t, out);
      for (int i = 0; i < 8; ++i) fputc(static_cast<int>((key >> (8 * i)) & 0xff), out);
      for (int i = 0; i < 4; ++i) fputc(static_cast<int>((len >> (8 * i)) & 0xff), out);
      fwrite(p, 1, len, out);
      ++n;
      return true;
    });
  }
  fclose(out);
  db.Close();
  std::filesystem::remove_all(folder);
  printf("%zu records\n", n);
  return n ? 0 : 1;
}
