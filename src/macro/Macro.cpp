#include "Macro.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>

#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace mm {

namespace {

constexpr double kStepsPerSecond = 240.0;

std::string escape(std::string const& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += fmt::format("\\u{:04x}", static_cast<unsigned char>(c));
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

// A file name that survives every platform: letters, digits and dashes, and never
// empty. Two local levels can share a name, so the id goes on the end.
std::string fileStem(MacroInfo const& info) {
    std::string clean;
    for (char c : info.levelName) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            clean.push_back(c);
        } else if (c == ' ' || c == '-' || c == '_') {
            if (!clean.empty() && clean.back() != '-') clean.push_back('-');
        }
    }
    while (!clean.empty() && clean.back() == '-') clean.pop_back();
    if (clean.empty()) clean = "level";
    if (clean.size() > 48) clean.resize(48);
    return fmt::format("{}-{}", clean, info.levelId);
}

std::string plainText(std::vector<Change> const& changes, std::size_t steps,
                      MacroInfo const& info) {
    std::string out;
    out += "{\n";
    out += fmt::format(" \"level\": \"{}\",\n", escape(info.levelName));
    out += fmt::format(" \"levelID\": {},\n", info.levelId);
    out += " \"steps_per_second\": 240,\n";
    out += " \"note\": \"step counts physics steps from the start of the level; hold is the "
           "button state from that step until the next entry\",\n";
    out += fmt::format(" \"steps\": {},\n", steps);
    out += fmt::format(" \"complete\": {},\n", info.complete ? "true" : "false");
    out += fmt::format(" \"reached_percent\": {:.2f},\n", info.reachedPercent);
    out += " \"changes\": [\n";
    for (std::size_t index = 0; index < changes.size(); ++index) {
        out += fmt::format("  {{\"step\": {}, \"hold\": {}}}{}\n",
                           changes[index].step + info.startOffset,
                           changes[index].hold ? "true" : "false",
                           index + 1 == changes.size() ? "" : ",");
    }
    out += " ]\n}\n";
    return out;
}

std::string gdrText(std::vector<Change> const& changes, std::size_t steps,
                    MacroInfo const& info) {
    std::string out;
    out += "{\n";
    out += " \"gameVersion\": 2.207,\n";
    out += fmt::format(" \"description\": \"Macro Maker route for {}\",\n",
                       escape(info.levelName));
    out += fmt::format(" \"duration\": {:.4f},\n", static_cast<double>(steps) / kStepsPerSecond);
    out += " \"botInfo\": {\"name\": \"Macro Maker\", \"version\": \"1.0.0\"},\n";
    out += fmt::format(" \"levelInfo\": {{\"id\": {}, \"name\": \"{}\"}},\n", info.levelId,
                       escape(info.levelName));
    out += " \"author\": \"Macro Maker\",\n";
    out += " \"framerate\": 240.0,\n";
    out += " \"seed\": 0,\n";
    out += " \"coins\": 0,\n";
    out += " \"ldm\": false,\n";
    out += " \"inputs\": [\n";
    for (std::size_t index = 0; index < changes.size(); ++index) {
        out += fmt::format("  {{\"frame\": {}, \"btn\": 1, \"2p\": false, \"down\": {}}}{}\n",
                           changes[index].step + info.startOffset,
                           changes[index].hold ? "true" : "false",
                           index + 1 == changes.size() ? "" : ",");
    }
    out += " ]\n}\n";
    return out;
}

std::string mhrText(std::vector<Change> const& changes, MacroInfo const& info) {
    std::string out;
    out += "{\n";
    out += " \"meta\": {\"fps\": 240.0},\n";
    out += " \"events\": [\n";
    for (std::size_t index = 0; index < changes.size(); ++index) {
        out += fmt::format("  {{\"frame\": {}, \"hold\": {}, \"player2\": false}}{}\n",
                           changes[index].step + info.startOffset,
                           changes[index].hold ? "true" : "false",
                           index + 1 == changes.size() ? "" : ",");
    }
    out += " ]\n}\n";
    return out;
}

bool put(std::filesystem::path const& path, std::string const& text, MacroFiles& files) {
    auto written = utils::file::writeString(path, text);
    if (written.isErr()) {
        files.error = written.unwrapErr();
        return false;
    }
    files.written.push_back(path.string());
    return true;
}

} // namespace

std::vector<Change> changesOf(std::vector<char> const& inputs) {
    std::vector<Change> out;
    bool held = false;
    for (int step = 0; step < static_cast<int>(inputs.size()); ++step) {
        bool want = inputs[static_cast<std::size_t>(step)] != 0;
        if (want != held) {
            held = want;
            out.push_back(Change{step, held});
        }
    }
    return out;
}

MacroFiles writeMacros(std::vector<char> const& inputs, MacroInfo const& info,
                       MacroFormats const& formats) {
    MacroFiles files;

    auto* mod = Mod::get();
    if (!mod) {
        files.error = "the mod is not loaded";
        return files;
    }

    auto folder = mod->getSaveDir() / "macros";
    std::error_code code;
    std::filesystem::create_directories(folder, code);

    std::vector<Change> changes = changesOf(inputs);
    std::string stem = fileStem(info);

    struct Written {
        char const* suffix;
        std::string text;
        bool wanted;
    };

    std::vector<Written> jobs;
    jobs.push_back({".gdr.json", gdrText(changes, inputs.size(), info), formats.gdr});
    jobs.push_back({".mhr.json", mhrText(changes, info), formats.mhr});
    jobs.push_back({".json", plainText(changes, inputs.size(), info), formats.plain});

    for (auto const& job : jobs) {
        if (!job.wanted) continue;
        if (!put(folder / (stem + job.suffix), job.text, files)) return files;
    }

    if (files.written.empty()) {
        files.error = "no macro format is turned on";
        return files;
    }

    // A second copy wherever the bot already looks, so nothing has to be moved by hand.
    if (!formats.extraFolder.empty()) {
        std::filesystem::path extra(formats.extraFolder);
        std::filesystem::create_directories(extra, code);
        if (!code) {
            for (auto const& job : jobs) {
                if (!job.wanted) continue;
                auto path = extra / (stem + job.suffix);
                auto written = utils::file::writeString(path, job.text);
                if (written.isOk()) {
                    files.written.push_back(path.string());
                } else {
                    log::warn("[macro-maker] could not write to {}: {}", path.string(),
                              written.unwrapErr());
                }
            }
        } else {
            log::warn("[macro-maker] could not make the folder {}: {}", formats.extraFolder,
                      code.message());
        }
    }

    return files;
}

} // namespace mm
