#pragma once
// Legit mode: one live take of a recording.
//
// The file is the performance. A take displaces it a little, the way one player
// differs between two evenings with the same piece: a slow tempo drift that
// comes back, a few milliseconds per note, velocity, note length, and for the
// looser players a rare dropped inner note and a hesitation that is caught up
// afterwards. It is a pure function of the score, the settings, the seed and
// the speed, so a take can be rebuilt mid-song and lands where it was, and a
// test can call it without a keyboard. Nothing here sleeps or sends anything.
// See LEGIT-MODE.md.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace legit {

enum class Player : uint8_t { Pro, Student, Beginner };
enum class Hands : uint8_t { Both, Right, Left };

struct Settings {
    bool humanise = false;   // the Legit Mode switch; hands apply without it
    Player player = Player::Pro;
    double difficulty = 0;   // 0..1, how much the piece asks of this player
    double timing = 0;       // 0..1 each
    double tempo = 0;
    double dynamics = 0;
    double length = 0;
    double mistakes = 0;
    Hands hands = Hands::Both;
    int split = -1;          // MIDI note the hands divide at; -1 takes the estimate
};

// Where the sliders sit for each assumed player.
inline Settings Defaults(Player player) {
    Settings s;
    s.player = player;
    switch (player) {
    case Player::Pro:      s.timing = .15; s.tempo = .15; s.dynamics = .20; s.length = .20; s.mistakes = 0;   break;
    case Player::Student:  s.timing = .40; s.tempo = .35; s.dynamics = .45; s.length = .45; s.mistakes = .30; break;
    case Player::Beginner: s.timing = .75; s.tempo = .70; s.dynamics = .75; s.length = .70; s.mistakes = .70; break;
    }
    return s;
}

struct ScoreEvent {
    int64_t time = 0;     // nanoseconds of score time
    int pitch = -1;       // MIDI note, or -1 for the pedal
    bool press = false;
    int velocity = 0;
    int track = -1;
};

struct TakeEvent {
    int64_t time = 0;     // nanoseconds of score time, displaced
    int velocity = 0;
    bool skip = false;    // a dropped note, or the silent hand
    bool right = true;
    int mate = -1;        // a press's release and a release's press, or -1
};

struct Take {
    std::vector<TakeEvent> events;   // one per score event, same order
    int split = 60;                  // the split the hands used
    bool splitByTrack = false;
};

// Full scale of each slider, in wall-clock terms.
constexpr double kTimingMs = 16;      // per-note sigma
constexpr double kTempoMs = 45;       // drift sigma
constexpr double kPhraseVelocity = 12;
constexpr double kNoteVelocity = 9;
constexpr double kLengthShare = .24;  // sigma as a share of the note's length
constexpr double kMistakeChance = .04;
constexpr int64_t kChordWindowNs = 35'000'000;
constexpr int64_t kMinimumHoldNs = 15'000'000;
constexpr int64_t kLiftBeforeRepeatNs = 3'000'000;

class Random {
    uint64_t state_;
public:
    explicit Random(uint64_t seed) : state_(seed) {}
    double unit() noexcept {   // uniform [0,1), splitmix64
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return static_cast<double>((z ^ (z >> 31)) >> 11) * (1.0 / 9007199254740992.0);
    }
    double normal() noexcept { // clamped to three sigma, so every offset has a bound
        const double u = std::max(unit(), 1e-12), v = unit();
        return std::clamp(std::sqrt(-2.0 * std::log(u)) * std::cos(6.283185307179586 * v), -3.0, 3.0);
    }
};

// A slow curve of unit variance that starts at zero: three mean-reverting walks
// at 2, 8 and 32 seconds on half-second knots, joined smoothly. Summed walks at
// spread time scales are the usual stand-in for the 1/f drift measured in
// human timing, and the knots keep the curve's slope far below one, so events
// displaced by it never change order.
class Drift {
    std::vector<double> knots_;
    static constexpr double kKnotSeconds = .5;
public:
    Drift(double seconds, uint64_t seed) {
        Random random(seed);
        const size_t count = static_cast<size_t>(std::max(0.0, seconds) / kKnotSeconds) + 3;
        knots_.resize(count);
        double walk[3] = {0, 0, 0};
        constexpr double tau[3] = {2, 8, 32};
        for (size_t i = 0; i < count; ++i) {
            double sum = 0;
            for (int k = 0; k < 3; ++k) {
                const double keep = std::exp(-kKnotSeconds / tau[k]);
                if (i) walk[k] = walk[k] * keep + std::sqrt(1 - keep * keep) * random.normal();
                sum += walk[k];
            }
            knots_[i] = sum / std::sqrt(3.0);
        }
    }
    double at(double seconds) const noexcept {
        const double position = std::clamp(seconds, 0.0, (knots_.size() - 2) * kKnotSeconds) / kKnotSeconds;
        const size_t i = static_cast<size_t>(position);
        const double f = position - i, smooth = f * f * (3 - 2 * f);
        return knots_[i] + (knots_[i + 1] - knots_[i]) * smooth;
    }
};

// An estimate and only that: where a fixed split between two hands would fall.
// Two-means over the pitches, the midpoint between the centres.
inline int EstimateSplit(const std::vector<ScoreEvent>& score) {
    double low = 48, high = 72;
    for (int pass = 0; pass < 12; ++pass) {
        double lowSum = 0, highSum = 0; size_t lowCount = 0, highCount = 0;
        for (const auto& e : score) {
            if (!e.press || e.pitch < 0) continue;
            if (std::abs(e.pitch - low) <= std::abs(e.pitch - high)) { lowSum += e.pitch; ++lowCount; }
            else { highSum += e.pitch; ++highCount; }
        }
        if (!lowCount || !highCount) break;
        low = lowSum / lowCount; high = highSum / highCount;
    }
    return std::clamp(static_cast<int>(std::lround((low + high) / 2)), 21, 108);
}

// Two tracks that both carry notes are the two hands as the file gives them.
inline bool SplitsByTrack(const std::vector<ScoreEvent>& score) {
    int first = INT32_MIN, second = INT32_MIN;
    for (const auto& e : score) {
        if (!e.press || e.pitch < 0 || e.track == first || e.track == second) continue;
        if (first == INT32_MIN) first = e.track;
        else if (second == INT32_MIN) second = e.track;
        else return false;
    }
    return second != INT32_MIN;
}

// An estimate and only that: notes per second, chord size and leaps, each
// against what a hard piece shows, at the speed it will be played.
inline double EstimateDifficulty(const std::vector<ScoreEvent>& score, double speed) {
    size_t presses = 0, groups = 0;
    int64_t first = 0, last = 0, groupStart = INT64_MIN;
    double leap = 0; int previousTop = -1;
    int top = -1;
    for (const auto& e : score) {
        if (!e.press || e.pitch < 0) continue;
        if (!presses) first = e.time;
        last = e.time; ++presses;
        if (groupStart == INT64_MIN || e.time - groupStart > kChordWindowNs) {
            if (top >= 0 && previousTop >= 0) leap += std::abs(top - previousTop);
            if (top >= 0) previousTop = top;
            groupStart = e.time; ++groups; top = e.pitch;
        } else top = std::max(top, e.pitch);
    }
    if (presses < 2 || last <= first || !groups) return 0;
    const double seconds = (last - first) / 1e9 / std::max(.05, speed);
    const double perSecond = presses / seconds, chord = static_cast<double>(presses) / groups, meanLeap = leap / groups;
    return std::clamp(.5 * perSecond / 14 + .25 * (chord - 1) / 2.5 + .25 * meanLeap / 9, 0.0, 1.0);
}

inline Take Build(const std::vector<ScoreEvent>& score, const Settings& settings, uint64_t seed, double speed) {
    Take take;
    const size_t count = score.size();
    take.events.resize(count);
    speed = std::max(.05, speed);
    for (size_t i = 0; i < count; ++i) {
        take.events[i].time = score[i].time;
        take.events[i].velocity = score[i].velocity;
    }
    if (!count) return take;

    // Pairs. A release closes the latest open press of its pitch and track.
    {
        std::unordered_map<int64_t, std::vector<int>> open;
        for (size_t i = 0; i < count; ++i) {
            const auto& e = score[i];
            if (e.pitch < 0) continue;
            auto& stack = open[(static_cast<int64_t>(e.track) << 8) | e.pitch];
            if (e.press) stack.push_back(static_cast<int>(i));
            else if (!stack.empty()) {
                take.events[i].mate = stack.back();
                take.events[stack.back()].mate = static_cast<int>(i);
                stack.pop_back();
            }
        }
    }

    // Hands. Two tracks that both carry notes are the hands as the file gives
    // them, the higher one the right. One track is divided by an estimate: two
    // hand centres follow the playing, a chord wider than a hand is cut at its
    // widest gap, and the user's split moves the boundary between them.
    {
        std::unordered_map<int, std::pair<double, size_t>> tracks;
        for (const auto& e : score) if (e.press && e.pitch >= 0) { tracks[e.track].first += e.pitch; ++tracks[e.track].second; }
        const int estimate = EstimateSplit(score);
        take.split = settings.split >= 0 ? settings.split : estimate;
        if (tracks.size() == 2) {
            take.splitByTrack = true;
            auto a = tracks.begin(), b = std::next(a);
            const int rightTrack = a->second.first / a->second.second >= b->second.first / b->second.second ? a->first : b->first;
            for (size_t i = 0; i < count; ++i) take.events[i].right = score[i].track == rightTrack;
        } else {
            const double shift = take.split - estimate;
            double left = estimate - 12.0, right = estimate + 12.0;
            size_t i = 0;
            while (i < count) {
                if (!score[i].press || score[i].pitch < 0) { ++i; continue; }
                std::vector<size_t> group;
                const int64_t start = score[i].time;
                size_t j = i;
                for (; j < count && score[j].time - start <= kChordWindowNs; ++j)
                    if (score[j].press && score[j].pitch >= 0) group.push_back(j);
                std::sort(group.begin(), group.end(), [&](size_t x, size_t y) { return score[x].pitch < score[y].pitch; });
                const double boundary = (left + right) / 2 + shift;
                size_t cut = group.size();   // notes below the cut go left
                const int span = score[group.back()].pitch - score[group.front()].pitch;
                if (group.size() > 1 && span > 14) {
                    int widest = -1;
                    for (size_t k = 1; k < group.size(); ++k) {
                        const int gap = score[group[k]].pitch - score[group[k - 1]].pitch;
                        if (gap > widest) { widest = gap; cut = k; }
                    }
                } else {
                    double mean = 0;
                    for (size_t k : group) mean += score[k].pitch;
                    cut = mean / group.size() >= boundary ? 0 : group.size();
                }
                double leftSum = 0, rightSum = 0;
                for (size_t k = 0; k < group.size(); ++k) {
                    const bool isRight = k >= cut;
                    take.events[group[k]].right = isRight;
                    (isRight ? rightSum : leftSum) += score[group[k]].pitch;
                }
                if (cut) left += .3 * (leftSum / cut - left);
                if (cut < group.size()) right += .3 * (rightSum / (group.size() - cut) - right);
                if (right - left < 7) { const double mid = (left + right) / 2; left = mid - 3.5; right = mid + 3.5; }
                i = j;
            }
            for (size_t k = 0; k < count; ++k)
                if (!score[k].press && take.events[k].mate >= 0) take.events[k].right = take.events[take.events[k].mate].right;
        }
        if (settings.hands != Hands::Both)
            for (size_t k = 0; k < count; ++k)
                if (score[k].pitch >= 0 && take.events[k].right != (settings.hands == Hands::Right)) take.events[k].skip = true;
    }
    if (!settings.humanise) return take;

    // Every component draws from its own stream and draws for every event, so
    // moving one slider rescales that component and leaves the take otherwise
    // where it was.
    const double seconds = score.back().time / 1e9;
    const Drift tempoDrift(seconds, seed ^ 0x1111), velocityDrift(seconds, seed ^ 0x2222);
    Random timing(seed ^ 0x3333), velocity(seed ^ 0x4444), length(seed ^ 0x5555),
        mistake(seed ^ 0x6666), hesitate(seed ^ 0x7777);

    // How much harder than the song's own average each moment is: presses in the
    // two seconds around it against the mean.
    std::vector<double> local(count, 1.0);
    {
        std::vector<int64_t> onsets;
        for (const auto& e : score) if (e.press && e.pitch >= 0) onsets.push_back(e.time);
        const double span = onsets.size() > 1 ? (onsets.back() - onsets.front()) / 1e9 : 0;
        const double mean = span > 0 ? onsets.size() / span : 0;
        size_t lo = 0, hi = 0;
        for (size_t i = 0; i < count && mean > 0; ++i) {
            while (lo < onsets.size() && onsets[lo] < score[i].time - 1'000'000'000) ++lo;
            while (hi < onsets.size() && onsets[hi] <= score[i].time + 1'000'000'000) ++hi;
            local[i] = std::clamp((hi - lo) / 2.0 / mean, .4, 2.5);
        }
    }
    const double bite = settings.player == Player::Pro ? .15 : settings.player == Player::Student ? .8 : 1.6;
    const auto amount = [&](size_t i) { return 1 + bite * std::clamp(settings.difficulty, 0.0, 1.0) * local[i]; };
    const auto wallMs = [&](double ms) { return static_cast<int64_t>(ms * 1e6 * speed); };

    // Hesitations, for the players that have them: a pause before a chord that
    // is caught up over the next second or two, never a stretch of the song.
    const double perSecond = settings.player == Player::Pro ? 0 : settings.player == Player::Student ? 1 / 40.0 : 1 / 10.0;
    const double pauseLow = settings.player == Player::Beginner ? 60 : 30, pauseHigh = settings.player == Player::Beginner ? 180 : 80;
    const double pauseScale = settings.timing / std::max(.01, Defaults(settings.player).timing);
    struct Pause { int64_t at; double ms; };
    std::vector<Pause> pauses;
    // Mistakes: an inner note of a chord of three or more, never two in two seconds.
    const double mistakeChance = kMistakeChance * settings.mistakes * settings.mistakes;
    int64_t lastMistake = INT64_MIN / 2, lastPause = INT64_MIN / 2, previousGroup = INT64_MIN;
    for (size_t i = 0; i < count;) {
        if (!score[i].press || score[i].pitch < 0) { ++i; continue; }
        const int64_t start = score[i].time;
        size_t j = i; int low = 128, high = -1, size = 0;
        for (; j < count && score[j].time - start <= kChordWindowNs; ++j)
            if (score[j].press && score[j].pitch >= 0) { low = std::min(low, score[j].pitch); high = std::max(high, score[j].pitch); ++size; }
        const double roll = hesitate.unit(), sizeRoll = hesitate.unit();
        if (previousGroup != INT64_MIN && perSecond > 0 && start - lastPause > 3'000'000'000) {
            const double gap = (start - previousGroup) / 1e9 / speed;
            if (roll < 1 - std::exp(-perSecond * pauseScale * amount(i) * gap)) {
                pauses.push_back({start, std::min(250.0, (pauseLow + (pauseHigh - pauseLow) * sizeRoll) * std::min(2.0, pauseScale))});
                lastPause = start;
            }
        }
        previousGroup = start;
        for (size_t k = i; k < j; ++k) {
            if (!score[k].press || score[k].pitch < 0) continue;
            const double drop = mistake.unit();
            if (size >= 3 && score[k].pitch != low && score[k].pitch != high && start - lastMistake > 2'000'000'000 &&
                drop < std::min(.08, mistakeChance * amount(k))) {
                take.events[k].skip = true;
                if (take.events[k].mate >= 0) take.events[take.events[k].mate].skip = true;
                lastMistake = start;
            }
        }
        i = j;
    }
    const auto lag = [&](int64_t time) {
        double ms = 0;
        for (const auto& pause : pauses)
            if (time >= pause.at) ms += pause.ms * std::exp(-((time - pause.at) / 1e9 / speed) / 1.5);
        return std::min(250.0, ms);
    };

    for (size_t i = 0; i < count; ++i) {
        const auto& e = score[i];
        const double a = amount(i), at = e.time / 1e9;
        const double noteRoll = timing.normal(), velocityRoll = velocity.normal(), lengthRoll = length.normal();
        // The drift and the lag move everything together: notes, releases and
        // the pedal keep their places against each other.
        // It is scaled by the song's difficulty and not the moment's, so two
        // neighbours are never moved by different amounts.
        const double whole = 1 + bite * std::clamp(settings.difficulty, 0.0, 1.0);
        const double ms = std::min(80.0, kTempoMs * settings.tempo * whole) * tempoDrift.at(at) + lag(e.time);
        int64_t time = e.time + wallMs(ms);
        if (e.pitch >= 0 && e.press) {
            time += wallMs(std::min(25.0, kTimingMs * settings.timing * a) * noteRoll);
            const double v = e.velocity + kPhraseVelocity * settings.dynamics * a * velocityDrift.at(at)
                + kNoteVelocity * settings.dynamics * a * velocityRoll;
            take.events[i].velocity = std::clamp(static_cast<int>(std::lround(v)), 1, 127);
        } else if (e.pitch >= 0 && take.events[i].mate >= 0) {
            const int64_t held = e.time - score[take.events[i].mate].time;
            time += static_cast<int64_t>(held * std::clamp(kLengthShare * settings.length * a * lengthRoll, -.4, .4));
        }
        take.events[i].time = std::max<int64_t>(0, time);
    }

    // A key is down for a moment at least, and is up before it is struck again
    // wherever the file had it up. Two tracks striking one key at one instant
    // stay one instant: the dispatcher sends that as a single strike.
    std::unordered_map<int, int> lastRelease, lastPress;
    for (size_t i = 0; i < count; ++i) {
        const auto& e = score[i];
        if (e.pitch < 0) continue;
        auto& event = take.events[i];
        if (e.press) {
            const auto twin = lastPress.find(e.pitch);
            if (twin != lastPress.end() && score[twin->second].time == e.time) {
                event.time = take.events[twin->second].time;
                lastPress[e.pitch] = static_cast<int>(i);
                continue;
            }
            lastPress[e.pitch] = static_cast<int>(i);
            const auto found = lastRelease.find(e.pitch);
            if (found != lastRelease.end() && score[found->second].time <= e.time) {
                auto& release = take.events[found->second];
                const int64_t floor = release.mate >= 0 ? take.events[release.mate].time + kMinimumHoldNs : 0;
                release.time = std::max(floor, std::min(release.time, event.time - kLiftBeforeRepeatNs));
                event.time = std::max(event.time, release.time + 1);
            }
        } else {
            if (event.mate >= 0) event.time = std::max(event.time, take.events[event.mate].time + kMinimumHoldNs);
            lastRelease[e.pitch] = static_cast<int>(i);
        }
    }
    return take;
}

} // namespace legit
