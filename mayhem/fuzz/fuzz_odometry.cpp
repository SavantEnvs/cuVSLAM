// Fuzz harness for the public cuVSLAM odometry API (libs/cuvslam/cuvslam2.h):
// Odometry(rig, cfg) construction (rig/camera/IMU calibration validation) followed by a short
// sequence of Track() calls (image/mask/depth validation, SOF feature tracking, pnp/sba,
// per-mode odometry pipelines) and the state getters.
//
// The input is decoded into a rig calibration (each float is either a "sane" value derived from a
// byte or a raw 32-bit float, so NaN/inf/huge values reach the API), an odometry Config, and up to
// 6 frames. Frames are crops of one fuzz-derived texture with a fuzzed per-frame shift, so features
// are actually tracked across frames. Optional masks and depth images have fuzzed dimensions.
//
// FUZZ_MODE selects the odometry mode (compile-time, one Mayhem target per mode):
//   0 Multicamera (stereo/multi-camera)   1 Mono   2 RGBD / Multisensor   3 Inertial (IMU)
//
// Invalid input is reported by the library by throwing std::invalid_argument/runtime_error
// (cuvslam2.h "@throws"), so exceptions are caught and the input is simply finished.
#include <fuzzer/FuzzedDataProvider.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <vector>

#include "cuvslam2.h"

#ifndef FUZZ_MODE
#define FUZZ_MODE 0
#endif

namespace {

float Flt(FuzzedDataProvider& p, float lo, float hi) {
  // 1 in 16: raw float bits (NaN, inf, denormals, huge); otherwise a value in [lo, hi].
  if ((p.ConsumeIntegral<uint8_t>() & 15) == 0) {
    uint32_t bits = p.ConsumeIntegral<uint32_t>();
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
  }
  return p.ConsumeFloatingPointInRange<float>(lo, hi);
}

cuvslam::Pose MakePose(FuzzedDataProvider& p, float tx) {
  cuvslam::Pose pose;
  if (p.ConsumeBool()) {
    for (auto& r : pose.rotation) r = Flt(p, -1.f, 1.f);
  }
  pose.translation = {tx, 0.f, 0.f};
  if (p.ConsumeBool()) {
    for (auto& t : pose.translation) t = Flt(p, -1.f, 1.f);
  }
  return pose;
}

struct Frame {
  std::vector<std::vector<uint8_t>> buffers;  // keeps pixel storage alive
};

}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  cuvslam::SetVerbosity(0);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 8) return -1;
  FuzzedDataProvider p(data, size);

  const int W = p.ConsumeIntegralInRange<int>(32, 128);
  const int H = p.ConsumeIntegralInRange<int>(24, 96);

#if FUZZ_MODE == 1
  const int num_cams = 1;
#elif FUZZ_MODE == 2
  const int num_cams = p.ConsumeIntegralInRange<int>(1, 2);
#elif FUZZ_MODE == 3
  const int num_cams = 2;
#else
  const int num_cams = p.ConsumeIntegralInRange<int>(2, 4);
#endif

  cuvslam::Rig rig;
  for (int c = 0; c < num_cams; ++c) {
    cuvslam::Camera cam{};
    cam.size = {W, H};
    if ((p.ConsumeIntegral<uint8_t>() & 15) == 0) {
      cam.size = {p.ConsumeIntegralInRange<int32_t>(-2, 160), p.ConsumeIntegralInRange<int32_t>(-2, 120)};
    }
    cam.focal = {Flt(p, 20.f, 300.f), Flt(p, 20.f, 300.f)};
    cam.principal = {Flt(p, 0.f, (float)W), Flt(p, 0.f, (float)H)};
#if FUZZ_MODE == 1
    if (p.ConsumeBool()) cam.rig_from_camera = MakePose(p, 0.f);
#else
    cam.rig_from_camera = MakePose(p, 0.12f * c);
#endif
    cam.distortion.model = static_cast<cuvslam::Distortion::Model>(p.ConsumeIntegralInRange<int>(0, 4));
    const int nparams = p.ConsumeIntegralInRange<int>(0, 12);
    for (int i = 0; i < nparams; ++i) cam.distortion.parameters.push_back(Flt(p, -0.5f, 0.5f));
    if (p.ConsumeBool()) {
      cam.border_top = p.ConsumeIntegralInRange<int32_t>(-4, H);
      cam.border_bottom = p.ConsumeIntegralInRange<int32_t>(-4, H);
      cam.border_left = p.ConsumeIntegralInRange<int32_t>(-4, W);
      cam.border_right = p.ConsumeIntegralInRange<int32_t>(-4, W);
    }
    rig.cameras.push_back(cam);
  }

  cuvslam::Odometry::Config cfg;
  cfg.use_gpu = false;
  cfg.async_sba = false;  // deterministic, single-threaded SBA
  cfg.multicam_mode = static_cast<cuvslam::Odometry::MulticameraMode>(p.ConsumeIntegralInRange<int>(0, 2));
  cfg.use_motion_model = p.ConsumeBool();
  cfg.use_denoising = p.ConsumeBool();
  cfg.rectified_stereo_camera = p.ConsumeBool();
  cfg.enable_observations_export = true;
  cfg.enable_landmarks_export = true;
  cfg.enable_final_landmarks_export = p.ConsumeBool();
  if (p.ConsumeBool()) cfg.max_frame_delta_s = Flt(p, 0.01f, 5.f);
  if (p.ConsumeBool()) {
    cfg.min_depth = Flt(p, -1.f, 2.f);
    cfg.max_depth = Flt(p, -1.f, 50.f);
  }
#if FUZZ_MODE == 1
  cfg.odometry_mode = cuvslam::Odometry::OdometryMode::Mono;
#elif FUZZ_MODE == 2
  const bool multisensor = p.ConsumeBool();
  if (multisensor) {
    cfg.odometry_mode = cuvslam::Odometry::OdometryMode::Multisensor;
    const int nd = p.ConsumeIntegralInRange<int>(0, num_cams);
    for (int i = 0; i < nd; ++i) cfg.multisensor_settings.depth_camera_ids.push_back(p.ConsumeIntegralInRange<int>(-1, num_cams));
    cfg.multisensor_settings.depth_scale_factor = Flt(p, 0.1f, 5000.f);
    cfg.multisensor_settings.enable_depth_stereo_tracking = p.ConsumeBool();
  } else {
    cfg.odometry_mode = cuvslam::Odometry::OdometryMode::RGBD;
    cfg.rgbd_settings.depth_camera_id = p.ConsumeIntegralInRange<int>(-1, num_cams);
    cfg.rgbd_settings.depth_scale_factor = Flt(p, 0.1f, 5000.f);
    cfg.rgbd_settings.enable_depth_stereo_tracking = p.ConsumeBool();
  }
#elif FUZZ_MODE == 3
  cfg.odometry_mode = cuvslam::Odometry::OdometryMode::Inertial;
  cfg.debug_imu_mode = p.ConsumeBool();
  {
    cuvslam::ImuCalibration imu{};
    imu.rig_from_imu = MakePose(p, 0.f);
    imu.gyroscope_noise_density = Flt(p, 1e-5f, 1e-1f);
    imu.gyroscope_random_walk = Flt(p, 1e-6f, 1e-2f);
    imu.accelerometer_noise_density = Flt(p, 1e-4f, 1e-1f);
    imu.accelerometer_random_walk = Flt(p, 1e-5f, 1e-2f);
    imu.frequency = Flt(p, 10.f, 1000.f);
    rig.imus.push_back(imu);
  }
#else
  cfg.odometry_mode = cuvslam::Odometry::OdometryMode::Multicamera;
#endif

  // Texture the frames are cropped from: (2W x 2H), from the input bytes (tiled if short).
  const int TW = 2 * W, TH = 2 * H;
  std::vector<uint8_t> tex_bytes = p.ConsumeBytes<uint8_t>(p.ConsumeIntegralInRange<size_t>(1, 4096));
  if (tex_bytes.empty()) tex_bytes.push_back(0x80);
  std::vector<uint8_t> tex(static_cast<size_t>(TW) * TH);
  for (int y = 0; y < TH; ++y)
    for (int x = 0; x < TW; ++x) {
      // Blocky pattern so corners exist even with few input bytes.
      size_t idx = (static_cast<size_t>(y / 4) * (TW / 4) + x / 4) % tex_bytes.size();
      tex[static_cast<size_t>(y) * TW + x] = tex_bytes[idx];
    }

  std::unique_ptr<cuvslam::Odometry> odom;
  try {
    odom = std::make_unique<cuvslam::Odometry>(rig, cfg);
  } catch (const std::exception&) {
    return 0;
  }

  const int nframes = p.ConsumeIntegralInRange<int>(1, 6);
  int sx = W / 4, sy = H / 4;
  int64_t ts = 1000000;
  for (int f = 0; f < nframes; ++f) {
    Frame keep;
    sx = std::min(std::max(sx + p.ConsumeIntegralInRange<int>(-3, 3), 0), W / 2);
    sy = std::min(std::max(sy + p.ConsumeIntegralInRange<int>(-3, 3), 0), H / 2);
    ts += p.ConsumeIntegralInRange<int64_t>(-1000, 70000000);

#if FUZZ_MODE == 3
    const int nimu = p.ConsumeIntegralInRange<int>(0, 12);
    for (int i = 0; i < nimu; ++i) {
      cuvslam::ImuMeasurement m{};
      m.timestamp_ns = ts - p.ConsumeIntegralInRange<int64_t>(-1000, 60000000);
      m.linear_accelerations = {Flt(p, -20.f, 20.f), Flt(p, -20.f, 20.f), Flt(p, -20.f, 20.f)};
      m.angular_velocities = {Flt(p, -5.f, 5.f), Flt(p, -5.f, 5.f), Flt(p, -5.f, 5.f)};
      try {
        odom->RegisterImuMeasurement(p.ConsumeIntegralInRange<uint32_t>(0, 1), m);
      } catch (const std::exception&) {
      }
    }
#endif

    cuvslam::Odometry::ImageSet images, masks, depths;
    const bool rgb = (p.ConsumeIntegral<uint8_t>() & 7) == 0;
    for (int c = 0; c < num_cams; ++c) {
      if (num_cams > 1 && (p.ConsumeIntegral<uint8_t>() & 31) == 0) continue;  // dropped frame
      const int ox = std::min(sx + c * 3, TW - W);
      std::vector<uint8_t> px(static_cast<size_t>(W) * H * (rgb ? 3 : 1));
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
          uint8_t v = tex[static_cast<size_t>(y + sy) * TW + x + ox];
          if (rgb) {
            px[(static_cast<size_t>(y) * W + x) * 3 + 0] = v;
            px[(static_cast<size_t>(y) * W + x) * 3 + 1] = v;
            px[(static_cast<size_t>(y) * W + x) * 3 + 2] = v;
          } else {
            px[static_cast<size_t>(y) * W + x] = v;
          }
        }
      cuvslam::Image img{};
      img.pixels = px.data();
      img.width = W;
      img.height = H;
      img.pitch = W * (rgb ? 3 : 1);
      img.encoding = rgb ? cuvslam::ImageData::Encoding::RGB : cuvslam::ImageData::Encoding::MONO;
      img.data_type = cuvslam::ImageData::DataType::UINT8;
      img.is_gpu_mem = false;
      img.timestamp_ns = ts;
      img.camera_index = static_cast<uint32_t>(c);
      if ((p.ConsumeIntegral<uint8_t>() & 31) == 0) img.camera_index = p.ConsumeIntegral<uint8_t>() % 6;
      images.push_back(img);
      keep.buffers.push_back(std::move(px));

      // Optional mask (fuzzed resolution, nearest-neighbour resized by the library).
      if ((p.ConsumeIntegral<uint8_t>() & 3) == 0) {
        const int mw = p.ConsumeIntegralInRange<int>(1, 2 * W), mh = p.ConsumeIntegralInRange<int>(1, 2 * H);
        std::vector<uint8_t> mpx(static_cast<size_t>(mw) * mh);
        const uint8_t pat = p.ConsumeIntegral<uint8_t>();
        for (size_t i = 0; i < mpx.size(); ++i) mpx[i] = ((i * 7 + pat) % 11 == 0) ? 255 : 0;
        cuvslam::Image m{};
        m.pixels = mpx.data();
        m.width = mw;
        m.height = mh;
        m.pitch = mw;
        m.encoding = cuvslam::ImageData::Encoding::MONO;
        m.data_type = cuvslam::ImageData::DataType::UINT8;
        if ((p.ConsumeIntegral<uint8_t>() & 15) == 0) {
          m.encoding = static_cast<cuvslam::ImageData::Encoding>(p.ConsumeIntegralInRange<int>(0, 1));
          m.data_type = static_cast<cuvslam::ImageData::DataType>(p.ConsumeIntegralInRange<int>(0, 2));
          // Keep the buffer large enough for the declared type so a crash is the library's, not ours.
          mpx.resize(static_cast<size_t>(mw) * mh * 4 * 3);
          m.pixels = mpx.data();
        }
        m.timestamp_ns = ts;
        m.camera_index = static_cast<uint32_t>(c);
        masks.push_back(m);
        keep.buffers.push_back(std::move(mpx));
      }

#if FUZZ_MODE == 2
      // Depth image (UINT16 or FLOAT32), usually matching the camera size.
      if (c == 0 || p.ConsumeBool()) {
        int dw = W, dh = H;
        if ((p.ConsumeIntegral<uint8_t>() & 7) == 0) {
          dw = p.ConsumeIntegralInRange<int>(1, 2 * W);
          dh = p.ConsumeIntegralInRange<int>(1, 2 * H);
        }
        const bool f32 = p.ConsumeBool();
        std::vector<uint8_t> dpx(static_cast<size_t>(dw) * dh * (f32 ? 4 : 2));
        const float base = Flt(p, 0.f, 10.f);
        for (int y = 0; y < dh; ++y)
          for (int x = 0; x < dw; ++x) {
            const uint8_t t = tex[static_cast<size_t>(y % TH) * TW + (x % TW)];
            if (f32) {
              float d = base + t / 64.f;
              std::memcpy(&dpx[(static_cast<size_t>(y) * dw + x) * 4], &d, 4);
            } else {
              uint16_t d = static_cast<uint16_t>(500 + t * 16);
              std::memcpy(&dpx[(static_cast<size_t>(y) * dw + x) * 2], &d, 2);
            }
          }
        cuvslam::Image d{};
        d.pixels = dpx.data();
        d.width = dw;
        d.height = dh;
        d.pitch = dw * (f32 ? 4 : 2);
        d.encoding = cuvslam::ImageData::Encoding::MONO;
        d.data_type = f32 ? cuvslam::ImageData::DataType::FLOAT32 : cuvslam::ImageData::DataType::UINT16;
        d.timestamp_ns = ts;
        d.camera_index = static_cast<uint32_t>(c);
        depths.push_back(d);
        keep.buffers.push_back(std::move(dpx));
      }
#endif
    }

    try {
      cuvslam::PoseEstimate est = odom->Track(images, masks, depths);
      (void)est;
      for (int c = 0; c < num_cams; ++c) (void)odom->GetLastObservations(static_cast<uint32_t>(c));
      (void)odom->GetLastLandmarks();
      (void)odom->GetLastGravity();
#if FUZZ_MODE == 3
      (void)odom->GetImuState();
#endif
      cuvslam::Odometry::State st;
      odom->GetState(st);
    } catch (const std::exception&) {
    }
  }
  if (cfg.enable_final_landmarks_export) {
    try {
      (void)odom->GetFinalLandmarks();
    } catch (const std::exception&) {
    }
  }
  return 0;
}
