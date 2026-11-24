// Fuzz harness for the .edex dataset file reader (libs/edex/edex.cpp, edex::EdexFile::read), the
// JSON format every cuVSLAM tool (tracker, cuvslam_api_launcher, gravity_calc, undistort) loads.
// The input is written to a scratch file (EdexFile only reads from a path), parsed, and on success
// written back out and re-read, which exercises the serializer on whatever the parser accepted.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "edex/edex.h"

namespace {
std::string TmpPath(const char* suffix) {
  const char* d = getenv("TMPDIR");
  std::string tmpl = std::string(d && *d ? d : "/tmp") + "/fuzz_edex_XXXXXX";
  tmpl += suffix;
  std::string buf = tmpl;
  int fd = mkstemps(&buf[0], static_cast<int>(strlen(suffix)));
  if (fd < 0) abort();
  close(fd);
  return buf;
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string in = TmpPath(".edex");
  FILE* f = fopen(in.c_str(), "wb");
  if (!f) abort();
  if (size) fwrite(data, 1, size, f);
  fclose(f);

  try {
    cuvslam::edex::EdexFile edex;
    if (edex.read(in)) {
      const std::string out = TmpPath(".edex");
      if (edex.write(out)) {
        cuvslam::edex::EdexFile again;
        (void)again.read(out);
      }
      unlink(out.c_str());
    }
  } catch (const std::exception&) {
    // jsoncpp reports malformed documents by throwing Json::Exception (a std::exception).
  }
  unlink(in.c_str());
  return 0;
}
