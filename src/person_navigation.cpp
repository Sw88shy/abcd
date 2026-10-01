#include "person_navigation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
double overlap(const PersonBox& a, const PersonBox& b) {
    const double width = std::max(0.0, std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x));
    const double height = std::max(0.0, std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y));
    const double intersection = width * height;
    const double total = a.width * a.height + b.width * b.height - intersection;
    return total > 0 ? intersection / total : 0;
}
}

void PersonTracker::update(const std::vector<PersonBox>& detections, double now) {
    // Retain IDs across brief misses, but do not pretend to recognise re-entries.
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const PersonTrack& t) {
        return now - t.lastSeen > 2.0;
    }), tracks_.end());
    for (auto& t : tracks_) t.visible = false;
    struct Match { std::size_t track, detection; double score; };
    std::vector<Match> matches;
    for (std::size_t t = 0; t < tracks_.size(); ++t) {
        const auto& a = tracks_[t].box;
        for (std::size_t d = 0; d < detections.size(); ++d) {
            const auto& b = detections[d];
            const double distance = std::hypot((a.x + a.width / 2) - (b.x + b.width / 2),
                                               (a.y + a.height / 2) - (b.y + b.height / 2));
            const double ratio = b.height / std::max(a.height, 1e-6);
            const double iou = overlap(a, b);
            if (ratio >= 0.5 && ratio <= 2.0 && (iou >= 0.15 || distance < 0.12)) {
                matches.push_back({t, d, iou + 0.25 * (1 - std::min(distance, 1.0))});
            }
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        return a.score > b.score;
    });
    std::vector<bool> used(detections.size(), false);
    for (const auto& m : matches) {
        auto& t = tracks_[m.track];
        if (t.visible || used[m.detection]) continue;
        // Confirmation requires consecutive detections.
        t.hits = now - t.lastSeen > 0.75 ? 1 : std::min(t.hits + 1, 3);
        t.box = detections[m.detection];
        t.lastSeen = now;
        t.visible = true;
        used[m.detection] = true;
    }
    for (auto& t : tracks_) if (!t.visible) t.hits = 0;
    for (std::size_t d = 0; d < detections.size(); ++d) {
        if (!used[d]) tracks_.push_back({nextId_++, detections[d], now});
    }
}

void PersonTracker::markVisited(int id) {
    for (auto& t : tracks_) if (t.id == id) t.visited = true;
}

PersonNavigation::PersonNavigation(double speed, double turnSpeed, double stopHeight)
    : speed_(speed), turnSpeed_(turnSpeed) {
    if (!std::isfinite(speed) || speed <= 0 || speed > 1 ||
        !std::isfinite(turnSpeed) || turnSpeed <= 0 || turnSpeed > 1 ||
        !std::isfinite(stopHeight) || stopHeight <= 0 || stopHeight > 1) {
        throw std::invalid_argument("speed, turn-speed and stop-height must be in (0, 1].");
    }
}

DriveCommand PersonNavigation::update(PersonTracker& tracker, double now) {
    const auto& tracks = tracker.tracks();
    if (state_ == State::Search) {
        const PersonTrack* selected = nullptr;
        for (const auto& t : tracks) {
            if (!t.visible || t.visited || t.hits < 3) continue;
            // IDs encode first observation order. Detector output is confidence-sorted.
            if (!selected || t.id < selected->id) selected = &t;
        }
        if (!selected) return {turnSpeed_, -turnSpeed_};
        targetId_ = selected->id;
        state_ = State::Approach;
    }
    const auto target = std::find_if(tracks.begin(), tracks.end(), [&](const PersonTrack& t) {
        return t.id == targetId_;
    });
    if (target == tracks.end()) {
        state_ = State::Search;
        targetId_ = -1;
        return {};  // Stop before searching on the next frame.
    }
    if (!target->visible || target->hits < 3) {
        return {};  // Never drive toward a stale box.
    }
    // Continuous following: size never triggers arrival or reduces power.
    // Both tracks run forward; the inside track keeps at least 75% of speed.
    const double error = std::clamp(2 * (target->box.x + target->box.width / 2 - 0.5), -1.0, 1.0);
    return {speed_ * (1.0 - 0.25 * std::max(-error, 0.0)),
            speed_ * (1.0 - 0.25 * std::max(error, 0.0))};
}

std::string PersonNavigation::status() const {
    if (state_ == State::Search) return "Searching for unvisited ID";
    return "Approaching ID " + std::to_string(targetId_);
}
