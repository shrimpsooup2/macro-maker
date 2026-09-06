#include "Level.hpp"

#include <algorithm>
#include <cmath>

namespace mm {

std::vector<int> const Level::s_empty{};

namespace {

constexpr double kBucketSize = 100.0;

} // namespace

bool isBlocking(int kind) {
    return kind == KSolid || kind == KSlope || kind == KBreakable;
}

bool isDeadly(int kind) {
    return kind == KHazard || kind == KAnimatedHazard;
}

bool isOrb(int kind) {
    switch (kind) {
        case KYellowOrb:
        case KPinkOrb:
        case KGravOrb:
        case KGreenOrb:
        case KDropOrb:
        case KRedOrb:
        case KDashOrb:
        case KGravDashOrb:
        case KSpiderOrb:
            return true;
        default:
            return false;
    }
}

bool isPad(int kind) {
    return kind == KYellowPad || kind == KPinkPad || kind == KGravPad || kind == KRedPad ||
           kind == KSpiderPad;
}

int modeOfPortal(int kind) {
    switch (kind) {
        case KCubePortal: return Cube;
        case KShipPortal: return Ship;
        case KBallPortal: return Ball;
        case KUfoPortal: return Ufo;
        case KWavePortal: return Wave;
        case KRobotPortal: return Robot;
        case KSpiderPortal: return Spider;
        case KSwingPortal: return Swing;
        default: return -1;
    }
}

bool isPortalMode(int kind) {
    return modeOfPortal(kind) >= 0;
}

int speedOfId(int objectId) {
    switch (objectId) {
        case 200: return 0;
        case 201: return 1;
        case 202: return 2;
        case 203: return 3;
        case 1334: return 4;
        default: return -1;
    }
}

void Level::build() {
    std::sort(objects.begin(), objects.end(),
              [](Obj const& a, Obj const& b) { return a.x < b.x; });

    consumableCount = 0;
    for (auto& o : objects) {
        // Anything that fires once and is then spent needs a slot in the run's list of
        // what it has used. Solids and hazards never are.
        bool usable = isOrb(o.kind) || isPad(o.kind) || o.kind == KTeleport ||
                      o.kind == KGravPortalDown || o.kind == KGravPortalUp;
        o.consumable = usable ? consumableCount++ : -1;
    }

    double lowest = 0.0;
    double highest = 300.0;
    double leftMost = 0.0;
    double rightMost = 0.0;
    bool any = false;
    for (auto const& o : objects) {
        if (!any) {
            lowest = o.bottom();
            highest = o.top();
            leftMost = o.left();
            rightMost = o.right();
            any = true;
            continue;
        }
        lowest = std::min<double>(lowest, o.bottom());
        highest = std::max<double>(highest, o.top());
        leftMost = std::min<double>(leftMost, o.left());
        rightMost = std::max<double>(rightMost, o.right());
    }

    // How far outside its own contents a run may get before it has plainly left. A run
    // that flips gravity under an open sky rises at terminal velocity for ever; nothing
    // up there can hurt it, so a search that scores clear space highly fills its beam
    // with escapees, they all reach identical height and velocity, collapse into one
    // under the dominance check, and the whole thing ends holding a single run sailing
    // off the top. Three hundred units is about ten blocks.
    ceiling = highest + 300.0;
    floorLimit = std::min(lowest, kGroundTop) - 300.0;

    if (length <= 0.0) length = rightMost + 90.0;

    m_bucketBase = static_cast<int>(std::floor(leftMost / kBucketSize)) - 1;
    int top = static_cast<int>(std::floor(rightMost / kBucketSize)) + 1;
    m_buckets.assign(static_cast<std::size_t>(std::max(1, top - m_bucketBase + 1)),
                     std::vector<int>{});

    for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
        Obj const& o = objects[static_cast<std::size_t>(index)];
        int lo = static_cast<int>(std::floor(o.left() / kBucketSize)) - m_bucketBase;
        int hi = static_cast<int>(std::floor(o.right() / kBucketSize)) - m_bucketBase;
        int last = static_cast<int>(m_buckets.size()) - 1;
        lo = std::clamp(lo, 0, last);
        hi = std::clamp(hi, 0, last);
        for (int b = lo; b <= hi; ++b) m_buckets[static_cast<std::size_t>(b)].push_back(index);
    }

    m_spans.clear();
}

std::vector<int> const& Level::nearby(double x, double reach) const {
    if (m_buckets.empty()) return s_empty;

    int lo = static_cast<int>(std::floor((x - reach) / kBucketSize)) - m_bucketBase;
    int hi = static_cast<int>(std::floor((x + reach) / kBucketSize)) - m_bucketBase;
    int last = static_cast<int>(m_buckets.size()) - 1;
    if (hi < 0 || lo > last) return s_empty;
    lo = std::clamp(lo, 0, last);
    hi = std::clamp(hi, 0, last);

    // The answer depends only on which buckets the span covers, and a search asks for
    // the same handful of spans thousands of times a step, so they are kept. This is
    // the innermost loop of the whole thing: collision, the orb check and the two look
    // aheads all come through here.
    std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lo)) << 32) |
                        static_cast<std::uint32_t>(hi);
    auto found = m_spans.find(key);
    if (found != m_spans.end()) return found->second;

    std::vector<int> span;
    if (lo == hi) {
        span = m_buckets[static_cast<std::size_t>(lo)];
    } else {
        for (int b = lo; b <= hi; ++b) {
            auto const& bucket = m_buckets[static_cast<std::size_t>(b)];
            span.insert(span.end(), bucket.begin(), bucket.end());
        }
        std::sort(span.begin(), span.end());
        span.erase(std::unique(span.begin(), span.end()), span.end());
    }

    // The cache is unbounded on purpose: a level covers a few hundred bucket spans at
    // most, and the search revisits every one of them thousands of times.
    return m_spans.emplace(key, std::move(span)).first->second;
}

} // namespace mm
