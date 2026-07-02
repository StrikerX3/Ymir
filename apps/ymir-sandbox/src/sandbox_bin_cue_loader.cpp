#include <ymir/media/loader/loader_bin_cue.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <string>

void runBinCueLoaderSandbox(int argc, char **argv) {
    if (argc < 2) {
        fmt::println("missing path argument");
        return;
    }

    std::filesystem::path cuePath{argv[1]};
    ymir::media::Disc disc{};
    if (ymir::media::loader::bincue::Load(cuePath, disc, false,
                                          [](auto, std::string msg) { fmt::println("{}", msg); })) {
        fmt::println("Disc image loaded successfully");
    }
}
