// Fuzz harness for cuVSLAM's image file loaders (libs/utils/image_loader.cpp), the code the
// cuvslam_api_launcher/tracker replay uses to read every frame, mask and depth image named in a
// dataset: utils::ImageLoaderT::load (TGA via libs/common/tga.cpp, PNG and JPEG via
// libs/utils/image_io.cpp) and utils::DepthLoader::load (cnpy .npy and 16-bit PNG depth).
// Both loaders dispatch on the file extension, so the first input byte picks the loader+extension
// and the rest is written to a scratch file with that name.
// JPEG (LoadJpeg) is not selectable: it installs libjpeg's default jpeg_std_error(), whose
// error_exit calls exit() on any malformed stream, which ends an in-process fuzzer on every bad input.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "utils/image_loader.h"

namespace {
std::string TmpPath(const char* suffix) {
  const char* d = getenv("TMPDIR");
  std::string buf = std::string(d && *d ? d : "/tmp") + "/fuzz_img_XXXXXX" + suffix;
  int fd = mkstemps(&buf[0], static_cast<int>(strlen(suffix)));
  if (fd < 0) abort();
  close(fd);
  return buf;
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) return -1;
  static const char* kExt[] = {".tga", ".png", ".npy", ".png"};
  const unsigned sel = data[0] % 4;
  const std::string path = TmpPath(kExt[sel]);
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) abort();
  if (size > 1) fwrite(data + 1, 1, size - 1, f);
  fclose(f);

  try {
    if (sel < 2) {
      cuvslam::utils::ImageLoaderT loader;
      if (loader.load(path)) {
        const auto& img = loader.getImage();
        volatile uint64_t sum = 0;
        for (Eigen::Index i = 0; i < img.size(); ++i) sum += img.data()[i];
      }
    } else {
      cuvslam::utils::DepthLoader loader;
      loader.load(path.c_str());
      const auto& img = loader.getImage();
      volatile float sum = 0;
      for (Eigen::Index i = 0; i < img.size(); ++i) sum += img.data()[i];
    }
  } catch (const std::exception&) {
    // DepthLoader reports unreadable files by throwing (std::runtime_error from itself and cnpy).
  }
  unlink(path.c_str());
  return 0;
}
