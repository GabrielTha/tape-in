// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Harness entry point. With -fsanitize=fuzzer (TAPEIN_FUZZ=ON, clang)
// libFuzzer provides main(). Otherwise this header supplies a regression
// driver: every file (or every file in every directory) named on the
// command line is fed to LLVMFuzzerTestOneInput once, so the corpus doubles
// as a regression suite under ASan/UBSan in ordinary builds.

#pragma once

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

#if !defined(TAPEIN_FUZZING)

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

int runFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", path.string().c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    int files = 0;
    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path p(argv[i]);
        if (std::filesystem::is_directory(p)) {
            for (const auto& entry : std::filesystem::directory_iterator(p)) {
                if (entry.is_regular_file()) {
                    failures += runFile(entry.path());
                    ++files;
                }
            }
        } else {
            failures += runFile(p);
            ++files;
        }
    }
    std::printf("%s: %d inputs replayed, %d unreadable\n", argv[0], files, failures);
    return failures == 0 ? 0 : 1;
}

#endif
