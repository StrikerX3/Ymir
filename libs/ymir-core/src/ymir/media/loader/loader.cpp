#include <ymir/media/loader/loader.hpp>

#include <ymir/media/loader/loader_bin_cue.hpp>
#include <ymir/media/loader/loader_chd.hpp>
#include <ymir/media/loader/loader_img_ccd_sub.hpp>
#include <ymir/media/loader/loader_iso.hpp>
#include <ymir/media/loader/loader_mdf_mds.hpp>

namespace ymir::media {

bool LoadDisc(std::filesystem::path path, Disc &disc, bool preloadToRAM, CbLoaderMessage cbMsg) {
    // Sanity check: check that the file exists
    if (!std::filesystem::is_regular_file(path)) {
        cbMsg(MessageType::Error, "File not found");
        disc.Invalidate();
        return false;
    }

    auto fail = [&] {
        cbMsg(MessageType::Error, "Not a valid disc image format. Supported files are .CCD, .CHD, .CUE, .MDS and .ISO");
        return false;
    };

    // Abuse short-circuiting to pick the first matching loader with less verbosity
    return loader::chd::Load(path, disc, preloadToRAM, cbMsg) ||    //
           loader::bincue::Load(path, disc, preloadToRAM, cbMsg) || //
           loader::mdfmds::Load(path, disc, preloadToRAM, cbMsg) || //
           loader::ccd::Load(path, disc, preloadToRAM, cbMsg) ||    //
           loader::iso::Load(path, disc, preloadToRAM, cbMsg) ||    //
           fail();
}

} // namespace ymir::media
