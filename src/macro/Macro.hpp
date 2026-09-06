#pragma once

#include <string>
#include <vector>

// Writing a solved route out as a macro a bot can play back.
//
// The search works in physics steps: one step is one 240Hz step, the rate the game runs
// its physics at in 2.2, counted from the moment the run starts moving. That is the same
// clock every macro format uses, so the inputs need no resampling -- only the change
// points matter, since a macro records when the button goes down and up rather than its
// state every step.
//
// Three formats come out, because which one is right depends on the bot rather than on
// us. The plain one is the source of truth; the other two are that same data shaped for
// the two most common readers.

namespace mm {

struct MacroInfo {
    std::string levelName;
    int levelId = 0;
    bool complete = false;
    double reachedPercent = 0.0;

    // Steps the game spent getting the level moving before input meant anything. Every
    // frame number written out is shifted by this, so a bot counting from the start of
    // the level presses at the same moment we did.
    int startOffset = 0;
};

struct MacroFiles {
    std::vector<std::string> written;
    std::string error;
    bool ok() const { return error.empty(); }
};

struct MacroFormats {
    bool gdr = true;
    bool mhr = true;
    bool plain = false;
    std::string extraFolder;
};

// The steps where the button changes, which is all a macro records.
struct Change {
    int step = 0;
    bool hold = false;
};

std::vector<Change> changesOf(std::vector<char> const& inputs);

MacroFiles writeMacros(std::vector<char> const& inputs, MacroInfo const& info,
                       MacroFormats const& formats);

} // namespace mm
