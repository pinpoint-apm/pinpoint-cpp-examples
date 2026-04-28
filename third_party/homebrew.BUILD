# BUILD file for the @homebrew local_repository, pointing at /opt/homebrew.
# Each cc_library here is a header-only view of a system-installed library;
# linking is done by the wrapper cc_library in //third_party:BUILD.bazel
# via -l<name> + -L/opt/homebrew/lib in linkopts.
package(default_visibility = ["//visibility:public"])

# hiredis: redis_example.cpp uses `#include <hiredis.h>` directly, so include
# both `include/hiredis` (for the bare name) and `include` (for any
# `<hiredis/*.h>` style usage).
cc_library(
    name = "hiredis_hdrs",
    hdrs = glob(
        [
            "include/hiredis/*.h",
            "include/hiredis/adapters/*.h",
        ],
        allow_empty = True,
    ),
    includes = [
        "include",
        "include/hiredis",
    ],
)

# librdkafka — examples use `<librdkafka/rdkafkacpp.h>`.
cc_library(
    name = "rdkafka_hdrs",
    hdrs = glob(["include/librdkafka/*.h"], allow_empty = True),
    includes = ["include"],
)

# mongocxx + bsoncxx — Homebrew layout has both `v1/` (newer headers) and
# `v_noabi/` (compatibility-stable views). v_noabi headers cross-include
# `<bsoncxx/v1/detail/prelude.hpp>`, so plain `include/` must also be on the
# search path.
cc_library(
    name = "mongocxx_hdrs",
    hdrs = glob(
        [
            "include/mongocxx/**/*.hpp",
            "include/mongocxx/**/*.h",
            "include/bsoncxx/**/*.hpp",
            "include/bsoncxx/**/*.h",
        ],
        allow_empty = True,
    ),
    includes = [
        "include",
        "include/bsoncxx/v_noabi",
        "include/mongocxx/v_noabi",
    ],
)
