#include <ymir/hw/vdp/vdp.hpp>

#include <ymir/core/types.hpp>

#include <stb_image.h>
#include <stb_image_write.h>

#include <fmt/format.h>

struct sample_struct {
    const char *vramFile;
    const char *cramFile;
    const char *fbFile;
    int width;
    int height;
};

// clang-format off
const sample_struct g_samples[] = {
   // VRAM                      Color-RAM             HW-framebuffer as bmp     W    H
    { "srally3.bin",            "srally3_cram.bin",   "srally3.bmp",            352, 224 },
    { "gouraud_lines.bin",      "lzsscube_cram.bin",  "gouraud_lines.bmp",      320, 224 },
    { "twisted2.bin",           "lines_cram.bin",     "twisted2.bmp",           352, 224 },
    { "sprites2.bin",           "lines_cram.bin",     "sprites2.bmp",           352, 224 },
    { "sprites_anti.bin",       "lines_cram.bin",     "sprites_anti.bmp",       352, 224 },
    { "sprites_anti_r.bin",     "lines_cram.bin",     "sprites_anti_r.bmp",     352, 224 },
    { "sprites_horizontal.bin", "lines_cram.bin",     "sprites_horizontal.bmp", 352, 224 },
    { "twisted_horizontal.bin", "lines_cram.bin",     "twisted_horizontal.bmp", 352, 224 },
    { "twisted_box2.bin",       "lines_cram.bin",     "twisted_box2.bmp",       352, 224 },
    { "twisted_box3.bin",       "lines_cram.bin",     "twisted_box3.bmp",       352, 224 },
    { "pixel_scale.bin",        "lines_cram.bin",     "pixel_scale.bmp",        352, 224 },
    { "gouraud_short.bin",      "lzsscube_cram.bin",  "gouraud_short.bmp",      320, 224 },
    { "gouraud_test.bin",       "lzsscube_cram.bin",  "gouraud_test.bmp",       320, 224 },
    { "gouraud_test2.bin",      "lzsscube_cram.bin",  "gouraud_test2.bmp",      320, 224 },
    { "ninpen_rangers.bin",     "lzsscube_cram.bin",  "ninpen_rangers.bmp",     320, 224 }
};
// clang-format on

void runVDP1AccuracySandbox(int argc, char **argv) {
    if (argc < 2) {
        fmt::println("missing path argument");
        return;
    }
    std::filesystem::path testPath{argv[1]};

    fmt::println("Reading tests from {}", testPath);

    for (auto &test : g_samples) {
        fmt::println("{}x{}  {:22s}  {:18s} {}", test.width, test.height, test.vramFile, test.cramFile, test.fbFile);

        bool renderDone = false;
        ymir::core::Scheduler scheduler{};
        ymir::core::Configuration config{};
        config.video.threadedVDP1 = false;
        config.video.threadedVDP2 = false;
        config.system.videoStandard = ymir::core::config::sys::VideoStandard::NTSC;
        auto vdp = std::make_unique<ymir::vdp::VDP>(scheduler, config);
        vdp->GetRenderer().Callbacks.VDP1DrawFinished = {&renderDone,
                                                         [](void *ctx) { *static_cast<bool *>(ctx) = true; }};

        auto &probe = vdp->GetProbe();

        auto vramPath = testPath / test.vramFile;
        auto cramPath = testPath / test.cramFile;
        auto fbPath = testPath / test.fbFile;

        std::vector<uint8> cram{};

        {
            std::ifstream in{vramPath, std::ios::binary};
            if (!in) {
                fmt::println("WARNING: file {} not found", vramPath);
            }
            for (uint32 addr = 0; addr < ymir::vdp::kVDP1VRAMSize; ++addr) {
                const uint8 value = in.get();
                probe.VDP1WriteVRAM<uint8>(addr, value);
            }
        }
        {
            std::ifstream in{cramPath, std::ios::binary};
            if (!in) {
                fmt::println("WARNING: file {} not found", vramPath);
            }
            cram.resize(ymir::vdp::kVDP2CRAMSize);
            in.read((char *)cram.data(), ymir::vdp::kVDP2CRAMSize);
        }

        probe.VDP1WriteReg(0x00, 0); // TVMR
        probe.VDP1WriteReg(0x02, 3); // FBCR
        probe.VDP1WriteReg(0x04, 3); // PTMR
        probe.VDP1WriteReg(0x06, 0); // EWDR

        while (!renderDone) {
            const uint64 cycles = scheduler.NextCount();
            vdp->Advance(cycles);
            scheduler.Advance(cycles);
        }

        auto vdp1fb = vdp->VDP1GetDrawFramebuffer();
        std::vector<uint32> finalFB{};
        finalFB.resize(test.width * test.height);
        for (uint32 y = 0; y < test.height; ++y) {
            for (uint32 x = 0; x < test.width; ++x) {
                const uint32 fbOffset = (y * 512 + x) * sizeof(uint16);
                const uint16 spriteData = util::ReadBE<uint16>(&vdp1fb[fbOffset & 0x3FFFF]);

                // Assuming mixed mode and ignoring shadows for now
                uint32 r, g, b;
                if (bit::test<15>(spriteData)) {
                    // RGB data
                    r = bit::extract<0, 4>(spriteData) << 3u;
                    g = bit::extract<5, 9>(spriteData) << 3u;
                    b = bit::extract<10, 14>(spriteData) << 3u;
                } else if (spriteData == 0) {
                    // Transparent
                    r = 0xFF;
                    g = 0x00;
                    b = 0xFF;
                } else {
                    // Palette data
                    const uint16 colorData = util::ReadBE<uint16>(&cram[(spriteData << 1u) & 0xFFE]);
                    r = bit::extract<0, 4>(colorData) << 3u;
                    g = bit::extract<5, 9>(colorData) << 3u;
                    b = bit::extract<10, 14>(colorData) << 3u;
                }
                finalFB[y * test.width + x] = (0xFF << 24u) | (b << 16u) | (g << 8u) | (r << 0u);
            }
        }

        auto outPath = testPath / "out";
        auto filename = std::filesystem::path(test.fbFile).replace_extension("").string();
        auto outFile = outPath / fmt::format("{}-final.png", filename);
        std::filesystem::create_directories(outPath);
        stbi_write_png(outFile.string().c_str(), test.width, test.height, 4, finalFB.data(),
                       test.width * sizeof(uint32));

        int imgX, imgY, ch;
        stbi_uc *img = stbi_load(fbPath.string().c_str(), &imgX, &imgY, &ch, 4);
        std::vector<uint32> deltaFB = finalFB;
        if (img != nullptr) {
            auto refFile = outPath / fmt::format("{}-ref.png", filename);
            stbi_write_png(refFile.string().c_str(), imgX, imgY, 4, img, imgX * sizeof(uint32));

            bool hasDelta = false;
            for (uint32 i = 0; i < deltaFB.size(); ++i) {
                deltaFB[i] ^= reinterpret_cast<uint32 *>(img)[i];
                if (deltaFB[i] & 0xFFFFFF) {
                    deltaFB[i] |= 0xFF000000;
                    hasDelta = true;
                }
            }
            auto deltaFile = outPath / fmt::format("{}-delta.png", filename);
            if (hasDelta) {
                stbi_write_png(deltaFile.string().c_str(), test.width, test.height, 4, deltaFB.data(),
                               test.width * sizeof(uint32));
            } else {
                std::filesystem::remove(deltaFile);
            }
        } else {
            fmt::println("WARNING: file {} not found", fbPath);
        }

        stbi_image_free(img);
    }
}
