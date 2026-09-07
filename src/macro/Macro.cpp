#include "Macro.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace mm {

namespace {

constexpr double kStepsPerSecond = 240.0;

// 2.2081, the way GDR asks for it: the version number with its dot taken out.
constexpr int kGameVersion = 22081;

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

// --- GDR version 2, the binary one -------------------------------------------------
//
// Written to the format's own spec rather than to a guess: whole numbers and booleans
// are variable-length, seven bits at a time, low group first; floats and doubles are
// their raw bytes the other way up; strings carry their length in front of them. The
// order below is the order the format's own writer uses, and it was checked against
// files Eclipse Menu had already recorded before a byte of it was trusted.

void putVarint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    if (value == 0) {
        out.push_back(0);
        return;
    }
    while (value > 0) {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        if (value > 0) byte |= 0x80;
        out.push_back(byte);
    }
}

void putString(std::vector<std::uint8_t>& out, std::string const& text) {
    putVarint(out, text.size());
    out.insert(out.end(), text.begin(), text.end());
}

void putFloat(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xff));
    }
}

void putDouble(std::vector<std::uint8_t>& out, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xff));
    }
}

std::vector<std::uint8_t> gdr2Bytes(std::vector<Change> const& changes, std::size_t steps,
                                    MacroInfo const& info) {
    std::vector<std::uint8_t> out;
    out.push_back('G');
    out.push_back('D');
    out.push_back('R');
    putVarint(out, 2);                          // format version
    putString(out, "");                         // no per-input extension
    putString(out, "Macro Maker");              // author
    putString(out, "route for " + info.levelName);
    putFloat(out, static_cast<float>(static_cast<double>(steps) / kStepsPerSecond));
    putVarint(out, kGameVersion);
    putDouble(out, kStepsPerSecond);            // framerate: steps per second
    putVarint(out, 0);                          // seed
    putVarint(out, 0);                          // coins
    putVarint(out, 0);                          // low detail
    putVarint(out, 0);                          // platformer
    putString(out, "Macro Maker");              // bot name
    putVarint(out, 1);                          // bot version
    putVarint(out, static_cast<std::uint64_t>(info.levelId < 0 ? 0 : info.levelId));
    putString(out, info.levelName);
    putVarint(out, 0);                          // no replay extension
    putVarint(out, 0);                          // no deaths recorded
    putVarint(out, changes.size());             // inputs in total
    putVarint(out, changes.size());             // and all of them player one's

    // Each input is the gap since the last one with the button state in the bottom bit.
    std::uint64_t previous = 0;
    for (auto const& change : changes) {
        std::uint64_t frame = static_cast<std::uint64_t>(change.step + info.startOffset);
        std::uint64_t packed = ((frame - previous) << 1) | (change.hold ? 1ull : 0ull);
        putVarint(out, packed);
        previous = frame;
    }
    return out;
}

// --- GDR version 1, the JSON one ---------------------------------------------------
//
// Key for key what the format's reader asks for. It reads "bot" and "level", not
// "botInfo" and "levelInfo", and it will throw on a replay with no "version" in it --
// which is how the first macros this wrote came out unreadable by anything.
std::string gdrText(std::vector<Change> const& changes, std::size_t steps,
                    MacroInfo const& info) {
    std::string out;
    out += "{\n";
    out += " \"gameVersion\": 22.081,\n";
    out += " \"version\": 1.0,\n";
    out += fmt::format(" \"description\": \"Macro Maker route for {}\",\n",
                       escape(info.levelName));
    out += fmt::format(" \"duration\": {:.4f},\n", static_cast<double>(steps) / kStepsPerSecond);
    out += " \"bot\": {\"name\": \"Macro Maker\", \"version\": \"1.0.0\"},\n";
    out += fmt::format(" \"level\": {{\"id\": {}, \"name\": \"{}\"}},\n", info.levelId,
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

// Eclipse Menu keeps its macros beside our own folder, one level up.
std::filesystem::path eclipseFolder() {
    auto* mod = Mod::get();
    if (!mod) return {};
    return mod->getSaveDir().parent_path() / "eclipse.eclipse-menu" / "replays";
}

struct Job {
    char const* suffix;
    std::string text;                   // for the text formats
    std::vector<std::uint8_t> bytes;    // for the binary one
    bool binary = false;
    bool wanted = false;
};

bool put(std::filesystem::path const& path, Job const& job, MacroFiles& files, bool report) {
    std::string problem;
    if (job.binary) {
        auto written = utils::file::writeBinary(path, job.bytes);
        if (written.isErr()) problem = written.unwrapErr();
    } else {
        auto written = utils::file::writeString(path, job.text);
        if (written.isErr()) problem = written.unwrapErr();
    }

    if (!problem.empty()) {
        if (report) files.error = problem;
        else log::warn("[macro-maker] could not write {}: {}", path.string(), problem);
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

    std::vector<Job> jobs;
    jobs.push_back(
        Job{".gdr2", {}, gdr2Bytes(changes, inputs.size(), info), true, formats.gdr2});
    jobs.push_back(Job{".gdr.json", gdrText(changes, inputs.size(), info), {}, false, formats.gdr});
    jobs.push_back(Job{".mhr.json", mhrText(changes, info), {}, false, formats.mhr});
    jobs.push_back(
        Job{".json", plainText(changes, inputs.size(), info), {}, false, formats.plain});

    for (auto const& job : jobs) {
        if (!job.wanted) continue;
        if (!put(folder / (stem + job.suffix), job, files, true)) return files;
    }

    if (files.written.empty()) {
        files.error = "no macro format is turned on";
        return files;
    }

    // Straight into Eclipse Menu's own list, so there is nothing to move by hand: it
    // reads the binary format, and only from this folder.
    if (formats.toEclipse) {
        auto replays = eclipseFolder();
        if (!replays.empty() && std::filesystem::exists(replays.parent_path(), code)) {
            std::filesystem::create_directories(replays, code);
            for (auto const& job : jobs) {
                if (!job.binary) continue;
                put(replays / (stem + job.suffix), job, files, false);
            }
        }
    }

    // And a second copy wherever else the bot already looks.
    if (!formats.extraFolder.empty()) {
        std::filesystem::path extra(formats.extraFolder);
        std::filesystem::create_directories(extra, code);
        if (!code) {
            for (auto const& job : jobs) {
                if (!job.wanted) continue;
                put(extra / (stem + job.suffix), job, files, false);
            }
        } else {
            log::warn("[macro-maker] could not make the folder {}: {}", formats.extraFolder,
                      code.message());
        }
    }

    return files;
}

} // namespace mm
