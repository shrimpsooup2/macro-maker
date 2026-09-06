#include "Search.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace mm {

struct TrailChunk {
    std::shared_ptr<TrailChunk const> prev;
    std::uint64_t bits = 0;
};

Trail Trail::push(bool bit) const {
    Trail out = *this;
    if (out.m_count == 64) {
        auto sealed = std::make_shared<TrailChunk>();
        sealed->prev = out.m_head;
        sealed->bits = out.m_bits;
        out.m_head = sealed;
        out.m_bits = 0;
        out.m_count = 0;
    }
    if (bit) out.m_bits |= (1ull << out.m_count);
    ++out.m_count;
    ++out.m_length;
    return out;
}

void Trail::flatten(std::vector<char>& out) const {
    out.assign(static_cast<std::size_t>(m_length), 0);
    int index = m_length - 1;
    for (int i = m_count - 1; i >= 0 && index >= 0; --i) {
        out[static_cast<std::size_t>(index--)] = ((m_bits >> i) & 1ull) ? 1 : 0;
    }
    for (auto chunk = m_head; chunk && index >= 0; chunk = chunk->prev) {
        for (int i = 63; i >= 0 && index >= 0; --i) {
            out[static_cast<std::size_t>(index--)] = ((chunk->bits >> i) & 1ull) ? 1 : 0;
        }
    }
}

std::uint64_t cellOf(double x, double y) {
    std::int32_t cx = static_cast<std::int32_t>(std::floor(x / kCell));
    std::int32_t cy = static_cast<std::int32_t>(std::floor(y / kCell));
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
           static_cast<std::uint32_t>(cy);
}

void addAvoid(AvoidMap& map, double x, double y, double cost) {
    map[cellOf(x, y)] += cost;
}

std::vector<int> clickFrames(std::vector<char> const& inputs) {
    std::vector<int> out;
    bool held = false;
    for (int frame = 0; frame < static_cast<int>(inputs.size()); ++frame) {
        bool want = inputs[static_cast<std::size_t>(frame)] != 0;
        if (want != held) {
            held = want;
            out.push_back(frame);
        }
    }
    return out;
}

namespace {

using Clock = std::chrono::steady_clock;

// Is this a step where the button actually decides anything? A press changes nothing
// while a cube is in mid air, so those steps are not branched on. This is most of the
// speed the search has.
bool decision(Run const& run, int frame, int every) {
    int mode = run.p.mode;

    if (mode == Ship || mode == Wave) {
        // steered continuously: check in regularly
        return frame % every == 0;
    }

    if (mode == Cube || mode == Robot || mode == Ball || mode == Spider) {
        if (run.p.onGround) return true;
    } else if (mode == Ufo || mode == Swing) {
        if (frame % every == 0) return true;
    }

    // near an orb, a press is worth considering wherever the run is
    for (int index : run.level->nearby(run.x, 40.0)) {
        Obj const& o = run.level->objects[static_cast<std::size_t>(index)];
        if (!isOrb(o.kind)) continue;
        if (!run.isSpent(o) && run.overlaps(o)) return true;
    }
    return false;
}

// Whether the way is clear this far on, if the run holds roughly its course. Zero when
// nothing is in the way, negative for how badly it is. Clearance from hazards alone says
// nothing about walls, so on a level built out of solid tunnels every surviving state
// scores the same and the beam picks among them at random -- which is how a run that is
// perfectly placed to slip through a gap gets thrown away two hundred frames early in
// favour of one that is about to hit the wall beside it.
double room(Run const& run, double ahead) {
    double step = kXSpeed[std::clamp(run.p.speed, 0, 4)] / kStepsPerSecond;
    // Half the velocity, not all of it: over this many frames gravity has taken a large
    // bite out of a straight-line projection, and guessing high is worse than short.
    double y = run.p.y + run.p.vel * (ahead / step) * 0.45;
    double w = 0.0;
    double h = 0.0;
    run.playerBox(w, h);
    double bottom = y - h * 0.5;
    double top = y + h * 0.5;
    double x = run.x + ahead;

    double worst = 0.0;
    for (int index : run.level->nearby(x, 0.0)) {
        Obj const& o = run.level->objects[static_cast<std::size_t>(index)];
        if (o.left() > x || o.right() < x) continue;
        if (top <= o.bottom() || bottom >= o.top()) continue;
        double overlap = std::min<double>(top, o.top()) - std::max<double>(bottom, o.bottom());
        if (isDeadly(o.kind)) return -overlap - 60.0;
        // A ramp is something to ride, not something in the way.
        if (o.kind == KSlope) continue;
        // Clipping the top of a solid is landing on it, which is ordinary. Only being
        // deep inside one means the run is aimed at a wall rather than a floor.
        if (isBlocking(o.kind) && overlap > h * 0.5) worst = std::min(worst, h * 0.5 - overlap);
    }
    return worst;
}

// How much room the run has. Sorted on directly, so it decides the search.
double safety(Run const& run, AvoidMap const* avoid) {
    double worst = 200.0;
    double stray = 1e9;

    for (int index : run.level->nearby(run.x, 120.0)) {
        Obj const& o = run.level->objects[static_cast<std::size_t>(index)];
        double gap = std::abs(static_cast<double>(o.y) - run.p.y);
        if (gap < stray) stray = gap;               // nearest anything, deadly or not
        if (!isDeadly(o.kind)) continue;
        if (o.x < run.x) continue;
        if (gap < worst) worst = gap;
    }

    // Clearance stops being worth anything past a point, and climbing away from the
    // level is not safety: it is a slower death.
    worst = std::min(worst, 120.0);

    // Empty space scores perfectly on every other term, which is the trap: when a
    // section gets hard the beam fills up with states drifting away from the level, they
    // all reach the same height and velocity and collapse into one under the dominance
    // check, and the search ends holding a single run sailing off the top of the screen.
    // Capped, because "nothing anywhere near" is a sentinel rather than a distance.
    if (stray > 150.0) worst -= (std::min(stray, 450.0) - 150.0) / 8.0;

    double height = run.p.y - kGroundTop;
    if (height < 0.0) worst -= -height / 2.0;

    // Two looks ahead: one about a jump away, one about three. Weighted heavily enough
    // that a state on course to hit something always sorts below one that is not.
    double score = worst + 4.0 * (room(run, 45.0) + room(run, 105.0));

    // Places a route that failed went through, and places runs keep dying. Being in one
    // is not fatal and is not forbidden -- sometimes the way on really is through there
    // -- but it costs enough to send the beam looking elsewhere first.
    if (avoid && !avoid->empty()) {
        auto found = avoid->find(cellOf(run.x, run.p.y));
        if (found != avoid->end()) score -= found->second;
    }
    return score;
}

// Fill the beam without letting one height take all of it. Sorting on score alone is how
// the search loses: the states that have drifted clear of the level score well on every
// term, they crowd the beam, the dominance check merges them because they all reach the
// same height and velocity, and what is left is one run sailing away.
void spread(std::vector<Node>& ranked, int width, std::vector<Node>& out,
            double band = 45.0) {
    int cap = std::max(2, width / 6);
    std::unordered_map<int, int> counts;
    out.clear();
    std::vector<Node*> spill;

    for (auto& node : ranked) {
        // Spreading by gravity and velocity as well as height was tried and is worse: it
        // splits the beam so finely that each bucket holds too little to get through
        // anything tight.
        int bandIndex = static_cast<int>(std::floor(node.run.p.y / band));
        int& seen = counts[bandIndex];
        if (seen < cap) {
            ++seen;
            out.push_back(std::move(node));
            if (static_cast<int>(out.size()) >= width) return;
        } else {
            spill.push_back(&node);
        }
    }

    for (Node* node : spill) {
        if (static_cast<int>(out.size()) >= width) break;
        out.push_back(std::move(*node));
    }
}

std::uint64_t tollKey(Death const& death) {
    return (static_cast<std::uint64_t>(static_cast<int>(death.cause)) << 32) |
           static_cast<std::uint32_t>(death.objectId);
}

std::vector<DeathCount> rankDeaths(std::unordered_map<std::uint64_t, DeathCount> const& toll) {
    std::vector<DeathCount> out;
    out.reserve(toll.size());
    for (auto const& entry : toll) out.push_back(entry.second);
    std::sort(out.begin(), out.end(),
              [](DeathCount const& a, DeathCount const& b) { return a.times > b.times; });
    if (out.size() > 6) out.resize(6);
    return out;
}

SearchResult finish(Level const& level, Node const& best, bool solved, std::string note,
                    long long expansions,
                    std::unordered_map<std::uint64_t, DeathCount> const& toll) {
    SearchResult result;
    result.solved = solved;
    best.trail.flatten(result.inputs);
    result.reachedX = best.run.x;
    result.length = std::max(1.0, level.length);
    result.frames = static_cast<int>(result.inputs.size());
    result.expansions = expansions;
    result.note = std::move(note);
    result.deaths = rankDeaths(toll);
    result.lastDeath = best.run.death;
    result.deathX = static_cast<float>(best.run.x);
    result.deathY = static_cast<float>(best.run.p.y);
    return result;
}

// The cells a failed route used on its way into the wall, and where it ended. Only the
// approach is worth making expensive, not the whole rest of the route: give back twenty
// clicks and marking everything from there condemns thousands of units that were never
// the problem.
void lineCells(Level const& level, std::vector<char> const& inputs, int fromFrame,
               AvoidMap& into, double repulsion, double window = 2200.0) {
    Run run(level);
    std::vector<std::pair<double, double>> track;
    for (int frame = 0; frame < static_cast<int>(inputs.size()); ++frame) {
        run.step(inputs[static_cast<std::size_t>(frame)] != 0);
        if (frame >= fromFrame) track.emplace_back(run.x, run.p.y);
        if (run.dead || run.finished) break;
    }

    addAvoid(into, run.x, run.p.y, repulsion);
    if (track.empty()) return;

    double reach = std::min(window, run.x - track.front().first);
    double edge = run.x - reach;
    for (auto const& point : track) {
        if (point.first >= edge) addAvoid(into, point.first, point.second, repulsion);
    }
}

} // namespace

SearchResult solve(Level const& level, SearchOptions const& options, AvoidMap const* avoid,
                   Checkpoint const* seed, SearchProgress* progress,
                   std::atomic<bool> const* cancel, std::vector<Checkpoint>* marksOut) {
    auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                       std::chrono::duration<double>(std::max(0.5, options.seconds)));

    std::unordered_map<std::uint64_t, DeathCount> toll;
    std::vector<Checkpoint> marks;
    std::vector<Node> beam;
    int firstFrame = 0;

    if (seed && !seed->beam.empty()) {
        firstFrame = seed->frame;
        beam = seed->beam;
    } else {
        Node first{Run(level), Trail{}, 0.0};
        beam.push_back(std::move(first));
    }

    Node best = beam.front();
    long long expansions = 0;
    int every = std::max(1, options.every);
    int width = std::max(2, options.width);

    std::vector<Node> next;
    std::vector<Node> ranked;
    std::unordered_map<Run::Key, int, Run::KeyHash> seen;
    next.reserve(static_cast<std::size_t>(width) * 2u);

    for (int frame = firstFrame; frame < options.maxFrames; ++frame) {
        next.clear();

        for (auto const& node : beam) {
            bool choices[2] = {node.run.prevButton, false};
            int count = 1;
            if (decision(node.run, frame, every)) {
                choices[0] = false;
                choices[1] = true;
                count = 2;
            }

            for (int pick = 0; pick < count; ++pick) {
                Node child{node.run, Trail{}, 0.0};
                child.run.step(choices[pick]);
                ++expansions;
                if (child.run.dead) {
                    auto& entry = toll[tollKey(child.run.death)];
                    entry.death = child.run.death;
                    ++entry.times;
                    continue;
                }
                child.trail = node.trail.push(choices[pick]);
                child.score = safety(child.run, avoid);
                next.push_back(std::move(child));
            }
        }

        if (next.empty()) {
            std::string note = "the whole beam died";
            auto ranking = rankDeaths(toll);
            if (!ranking.empty()) {
                note = describe(ranking.front().death) + " (x" +
                       std::to_string(ranking.front().times) + ")";
            }
            if (marksOut) *marksOut = std::move(marks);
            return finish(level, best, false, note, expansions, toll);
        }

        // Keep the best of each equivalent state.
        seen.clear();
        for (int index = 0; index < static_cast<int>(next.size()); ++index) {
            Run::Key key = next[static_cast<std::size_t>(index)].run.key();
            auto found = seen.find(key);
            if (found == seen.end()) {
                seen.emplace(key, index);
            } else if (next[static_cast<std::size_t>(index)].score >
                       next[static_cast<std::size_t>(found->second)].score) {
                found->second = index;
            }
        }

        ranked.clear();
        ranked.reserve(seen.size());
        for (auto const& entry : seen) {
            ranked.push_back(std::move(next[static_cast<std::size_t>(entry.second)]));
        }

        // Ranking by x is meaningless here: the run travels at a fixed rate, so every
        // state alive at this frame is at the same place. What separates them is how
        // much room they have, so that is what the beam is sorted on.
        std::sort(ranked.begin(), ranked.end(),
                  [](Node const& a, Node const& b) { return a.score > b.score; });

        for (auto const& node : ranked) {
            if (node.run.finished) {
                if (marksOut) *marksOut = std::move(marks);
                return finish(level, node, true, "", expansions, toll);
            }
        }

        spread(ranked, width, beam);
        if (beam.empty()) {
            if (marksOut) *marksOut = std::move(marks);
            return finish(level, best, false, "nothing left in the beam", expansions, toll);
        }

        if (beam.front().run.x > best.run.x) best = beam.front();

        if (progress) {
            progress->frame.store(frame, std::memory_order_relaxed);
            progress->beamSize.store(static_cast<int>(beam.size()), std::memory_order_relaxed);
            progress->expansions.store(expansions, std::memory_order_relaxed);
            progress->x.store(beam.front().run.x, std::memory_order_relaxed);
            double seenBest = progress->bestX.load(std::memory_order_relaxed);
            if (best.run.x > seenBest) progress->bestX.store(best.run.x, std::memory_order_relaxed);
        }

        // A snapshot every so often, so a retry has somewhere to restart from. Now that
        // a state carries a link back rather than a copy of its history these are nearly
        // free, so enough are kept to cover a whole level.
        if (options.keep > 0 && frame % options.keep == 0) {
            marks.push_back(Checkpoint{frame, beam});
            if (marks.size() > 40) marks.erase(marks.begin());
        }

        if (expansions > options.budget) {
            if (marksOut) *marksOut = std::move(marks);
            return finish(level, best, false, "ran out of budget", expansions, toll);
        }
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            if (marksOut) *marksOut = std::move(marks);
            return finish(level, best, false, "stopped", expansions, toll);
        }
        if ((frame & 15) == 0 && Clock::now() > deadline) {
            if (marksOut) *marksOut = std::move(marks);
            return finish(level, best, false, "ran out of time", expansions, toll);
        }
    }

    if (marksOut) *marksOut = std::move(marks);
    return finish(level, best, false, "hit the frame limit", expansions, toll);
}

SearchResult solveLearning(Level const& level, SearchOptions options, AvoidMap const& known,
                           SearchProgress* progress, std::atomic<bool> const* cancel) {
    auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                       std::chrono::duration<double>(std::max(1.0, options.seconds)));
    auto left = [&] {
        double remaining =
            std::chrono::duration_cast<std::chrono::duration<double>>(deadline - Clock::now())
                .count();
        return std::max(1.0, remaining);
    };

    AvoidMap graveyard = known;
    SearchOptions round = options;
    round.seconds = left();

    std::vector<Checkpoint> marks;
    SearchResult best = solve(level, round, graveyard.empty() ? nullptr : &graveyard, nullptr,
                              progress, cancel, &marks);
    if (best.solved) return best;

    int givenBack = 1;
    int attempt = 0;

    while (Clock::now() < deadline) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;

        double wall = best.reachedX;
        std::vector<int> changes = clickFrames(best.inputs);
        if (marks.empty() || givenBack > static_cast<int>(changes.size())) break;

        if (givenBack > options.mostClicks) {
            // Past a couple of dozen decisions the retreat is no longer about how this
            // wall was approached, and each retry costs more while changing less. Start
            // over on the same wall with a wider beam and everything learned so far.
            givenBack = 1;
            round.width = std::min(round.width * 2, 3072);
            if (round.width >= 3072) break;
        }

        // Everything the failed route touched from the click being given back onwards.
        int fromFrame = changes[changes.size() - static_cast<std::size_t>(givenBack)];
        AvoidMap avoid = graveyard;
        lineCells(level, best.inputs, fromFrame, avoid, options.repulsion);
        // Ground that has swallowed a run stays expensive even after the route changes.
        addAvoid(graveyard, best.deathX, best.deathY, options.repulsion);

        Checkpoint const* seed = nullptr;
        for (auto const& mark : marks) {
            if (mark.frame <= fromFrame) seed = &mark;
        }

        ++attempt;
        if (progress) progress->round.store(attempt, std::memory_order_relaxed);

        round.seconds = left();
        std::vector<Checkpoint> freshMarks;
        SearchResult tried = solve(level, round, &avoid, seed, progress, cancel, &freshMarks);

        if (tried.reachedX > wall + 30.0) {
            best = std::move(tried);
            if (!freshMarks.empty()) marks = std::move(freshMarks);
            givenBack = 1;                  // new ground, and cheap until it argues back
            if (best.solved) return best;
        } else {
            ++givenBack;                    // same wall: give back more decisions
        }
    }

    return best;
}

} // namespace mm
