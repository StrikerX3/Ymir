#include "app/app.hpp"

#include <util/os_exception_handler.hpp>
#include <ymir/util/thread_name.hpp>

#include <cxxopts.hpp>
#include <fmt/format.h>

#include <memory>

int main(int argc, char **argv) {
#if defined(_WIN32)
    // NOTE: Setting the main thread name on Linux and macOS replaces the process name displayed on tools like `top`.
    util::SetCurrentThreadName("Main thread");
#endif

    bool showHelp = false;
    bool enableAllExceptions = false;

    app::CommandLineOptions progOpts{};
    cxxopts::Options options("Ymir", "Ymir - Sega Saturn emulator");
    options.add_options()("d,disc", "Path to Saturn disc image (.ccd, .chd, .cue, .iso, .mds)",
                          cxxopts::value(progOpts.gameDiscPath));
    options.add_options()("p,profile", "Path to profile directory", cxxopts::value(progOpts.profilePath));
    options.add_options()("u,user", "Force user profile",
                          cxxopts::value(progOpts.forceUserProfile)->default_value("false"));
    options.add_options()("h,help", "Display help text", cxxopts::value(showHelp)->default_value("false"));
    options.add_options()("f,fullscreen", "Start in fullscreen mode",
                          cxxopts::value(progOpts.fullScreen)->default_value("false"));
    options.add_options()("P,paused", "Start paused", cxxopts::value(progOpts.startPaused)->default_value("false"));
    options.add_options()("E,exceptions", "Capture all unhandled exceptions",
                          cxxopts::value(enableAllExceptions)->default_value("false"));
    options.parse_positional({"disc"});

    try {
        auto result = options.parse(argc, argv);
        if (showHelp) {
            fmt::println("{}", options.help());
            return 0;
        }

        util::RegisterExceptionHandler(enableAllExceptions);

        auto app = std::make_unique<app::App>();
        return app->Run(progOpts);
    } catch (const cxxopts::exceptions::exception &e) {
        std::string msg = fmt::format("Failed to parse arguments: {}", e.what());
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return -1;
    } catch (const std::system_error &e) {
        std::string msg = fmt::format("System error: {}", e.what());
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return e.code().value();
    } catch (const std::exception &e) {
        std::string msg = fmt::format("Unhandled exception: {}", e.what());
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return -1;
    } catch (...) {
        std::string msg = "Unspecified exception";
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return -1;
    }

    return 0;
}
