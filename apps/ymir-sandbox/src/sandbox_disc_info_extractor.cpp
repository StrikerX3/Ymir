#include <ymir/media/disc.hpp>
#include <ymir/media/loader/loader.hpp>

#include <fmt/format.h>
#include <fmt/std.h>

#include <filesystem>
#include <string>

void runDiscInfoExtractor(int argc, char **argv) {
    namespace fs = std::filesystem;
    if (argc <= 1) {
        fmt::println("missing path argument");
        return;
    }

    fs::path base{argv[1]};
    fmt::println("Scanning disc images in {}...", base);

    ymir::media::Disc disc{};
    if (fs::exists(base) && fs::is_directory(base)) {
        for (const auto &entry : fs::recursive_directory_iterator{base}) {
            const auto &path = entry.path();
            if (ymir::media::LoadDisc(path, disc, false,
                                      [&](ymir::media::MessageType category, std::string message) {})) {
                // const auto areaCode = ymir::media::AreaCodeToString(disc.header.compatAreaCode);
                // fmt::println("{:8s}  {:100}  {}", areaCode, path.filename(), disc.header.productNumber);
                fmt::println("{:16s}  {:100}", disc.header.rawCompatPeripherals, path.filename());
            }
        }
    }
}
