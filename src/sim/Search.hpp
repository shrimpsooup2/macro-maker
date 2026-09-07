#pragma once

#include "Level.hpp"
#include "Run.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Finding a way through a level.
//
// The pathfinder every other mod uses toggles the button at random frames and keeps
// whatever got furthest. That cannot solve a tight gap: the odds of a random sequence
// landing a two-frame window are negligible, and nothing it learns from a death informs
// the next attempt.
//
// This is a beam search over the run's own state. Three things make it work where random
// does not: it branches only where the button decides something, it throws away states
// that are interchangeable with a better one, and when the whole beam dies it gives back
// a click and makes the line that failed expensive to be on.

namespace mm {

// How finely places are remembered, for the map of where routes have already failed.
inline constexpr double kCell = 15.0;

using AvoidMap = std::unordered_map<std::uint64_t, double>;

std::uint64_t cellOf(double x, double y);
void addAvoid(AvoidMap& map, double x, double y, double cost);

// A route's button history, kept as a chain of 64-step words shared between the states
// that came from it. Copying the whole list per expansion is what kept the Python beam
// narrow: half a million expansions each duplicating a few thousand booleans is most of
// the work. A push here costs one word, and allocates once every 64 steps.
struct TrailChunk;

class Trail {
public:
    Trail push(bool bit) const;
    void flatten(std::vector<char>& out) const;
    int length() const { return m_length; }

private:
    std::shared_ptr<TrailChunk const> m_head;
    std::uint64_t m_bits = 0;
    int m_count = 0;
    int m_length = 0;
};

struct Node {
    Run run;
    Trail trail;
    double score = 0.0;
};

struct Checkpoint {
    int frame = 0;
    std::vector<Node> beam;
};

struct SearchOptions {
    int width = 192;
    int every = 3;              // how often the flying modes reconsider the button
    int maxFrames = 200000;
    double seconds = 60.0;      // wall clock ceiling for the whole attempt
    long long budget = 200000000;
    int keep = 600;             // how often a beam is filed away to restart from
    int mostClicks = 96;        // how far back the learning pass will give clicks up
    double repulsion = 500.0;
};

struct SearchProgress {
    std::atomic<int> frame{0};
    std::atomic<int> beamSize{0};
    std::atomic<int> round{0};
    std::atomic<long long> expansions{0};
    std::atomic<double> x{0.0};
    std::atomic<double> bestX{0.0};
};

struct DeathCount {
    Death death;
    int times = 0;
};

struct SearchResult {
    bool solved = false;
    std::vector<char> inputs;       // one entry per physics step, from the start
    double reachedX = 0.0;
    double length = 1.0;
    int frames = 0;
    long long expansions = 0;
    std::string note;
    std::vector<DeathCount> deaths; // most common first
    Death lastDeath;
    float deathX = 0.f;
    float deathY = 0.f;

    double progress() const { return length > 1.0 ? reachedX / length : 0.0; }
};

// One beam search. `seed` picks the search up from a beam an earlier attempt reached, so
// a section that failed can be retried at more expense without replaying everything
// before it.
SearchResult solve(Level const& level, SearchOptions const& options,
                   AvoidMap const* avoid = nullptr, Checkpoint const* seed = nullptr,
                   SearchProgress* progress = nullptr,
                   std::atomic<bool> const* cancel = nullptr,
                   std::vector<Checkpoint>* marksOut = nullptr);

// Treat dying in the same place twice as information rather than bad luck: give a click
// back, make the line that failed expensive, and search again. `known` seeds the map of
// expensive ground, which is how a death in the real game teaches the simulator.
SearchResult solveLearning(Level const& level, SearchOptions options, AvoidMap const& known,
                           SearchProgress* progress = nullptr,
                           std::atomic<bool> const* cancel = nullptr);

// The frames where the button changed, which are the decisions a route is made of.
std::vector<int> clickFrames(std::vector<char> const& inputs);

} // namespace mm
