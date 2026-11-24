# syntax=docker/dockerfile:1.7
# Commit image for cuVSLAM (CPU-only build, USE_CUDA=OFF) on the org base.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# Upstream's CMake FetchContent dependencies, pinned to the exact versions/commits and hashes its
# cmake/ext/*.cmake declare, pre-fetched so mayhem/build.sh configures fully offline
# (FETCHCONTENT_SOURCE_DIR_<NAME> + FETCHCONTENT_FULLY_DISCONNECTED). Source only; the project's
# own CMake builds them (out of tree, under /mayhem/build-*).
USER root
RUN set -eu; \
    fetch() { name="$1"; url="$2"; sha="$3"; d="/opt/toolchains/deps/$name/src"; \
      mkdir -p "$d"; curl -fsSL --retry 5 -o /tmp/dep.tgz "$url"; \
      echo "$sha  /tmp/dep.tgz" | sha256sum -c -; \
      tar -xzf /tmp/dep.tgz --strip-components=1 -C "$d"; rm -f /tmp/dep.tgz; }; \
    fetch eigen-3.4.1 https://gitlab.com/libeigen/eigen/-/archive/3.4.1/eigen-3.4.1.tar.gz b93c667d1b69265cdb4d9f30ec21f8facbbe8b307cf34c0b9942834c6d4fdbe2; \
    fetch gflags-2.3.0 https://github.com/gflags/gflags/archive/v2.3.0.tar.gz f619a51371f41c0ad6837b2a98af9d4643b3371015d873887f7e8d3237320b2f; \
    fetch googletest-1.17.0 https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz 65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c; \
    fetch libjpeg-turbo-3.1.3 https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.3/libjpeg-turbo-3.1.3.tar.gz 075920b826834ac4ddf97661cc73491047855859affd671d52079c6867c1c6c0; \
    fetch jsoncpp-1.9.6 https://github.com/open-source-parsers/jsoncpp/archive/1.9.6.tar.gz f93b6dd7ce796b13d02c108bc9f79812245a82e577581c4c9aabe57075c90ea2; \
    fetch spdlog-1.17.0 https://github.com/gabime/spdlog/archive/v1.17.0.tar.gz d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744; \
    fetch zlib-1.3.1 https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz 17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c; \
    fetch libpng-1.6.55 https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.55.tar.gz 71a2c5b1218f60c4c6d2f1954c7eb20132156cae90bdb90b566c24db002782a6; \
    fetch cnpy-4e8810b https://github.com/rogersce/cnpy/archive/4e8810b1a8637695171ed346ce68f6984e585ef4.tar.gz 5120abc54a564efa92c642cc0199cc4fd3f345901157de9fbbdcedbb34d28d8a; \
    fetch circularbuffer-cef6680 https://github.com/vinitjames/circularbuffer/archive/cef66805cb5424e27300a966becc7c2678117c27.tar.gz 2946ff3d4168b5ec6c5295e9702ffd9b14f996aed6c6f8320e7a690883d64575; \
    fetch dense_hash_map-74277fc https://github.com/Jiwan/dense_hash_map/archive/74277fc4813028ae4a9e8d9176788eb8001177a6.tar.gz 34bf447075f8035d75c741f1ebd704b6c4f6e78ce75ecb20046472e9976199ae; \
    fetch yaml-cpp-0.9.0 https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.tar.gz 25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a; \
    fetch lmdb-0.9.31 https://github.com/LMDB/lmdb/archive/refs/tags/LMDB_0.9.31.tar.gz dd70a8c67807b3b8532b3e987b0a4e998962ecc28643e1af5ec77696b081c9b0; \
    chmod -R a+rX,go-w /opt/toolchains/deps

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
