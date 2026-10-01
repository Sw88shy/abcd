#include "person_navigation.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool stopped(DriveCommand c) { return c.left == 0 && c.right == 0; }
PersonBox person(double centre, double height = 0.3) {
    return {centre - 0.1, 0.1, 0.2, height, 0.9};
}
void confirm(PersonTracker& t, const std::vector<PersonBox>& boxes, double now) {
    t.update(boxes, now); t.update(boxes, now + 0.1); t.update(boxes, now + 0.2);
}
}

int main() {
    try {
        PersonTracker tracker;
        PersonNavigation nav;
        auto command = nav.update(tracker, 0);
        require(command.left > 0 && command.right < 0, "Empty view should search by pivoting");
        tracker.update({person(0.5), person(0.85)}, 0.1);
        nav.update(tracker, 0.1);
        require(nav.targetId() == -1, "One detection must not lock a target");
        tracker.update({person(0.5), person(0.85)}, 0.2);
        tracker.update({person(0.5), person(0.85)}, 0.3);
        command = nav.update(tracker, 0.3);
        require(nav.targetId() == 1 && command.left > 0 && command.right > 0,
                "First confirmed ID should be approached");
        // Reordering detections must not reorder IDs or transfer the target.
        tracker.update({person(0.85), person(0.51)}, 0.4);
        command = nav.update(tracker, 0.4);
        require(nav.targetId() == 1 && command.left > command.right, "Steer right toward locked target");
        tracker.update({person(0.85)}, 0.5);
        require(stopped(nav.update(tracker, 0.5)), "Missing target should stop immediately");
        require(nav.targetId() == 1, "Brief miss should preserve target lock");
        confirm(tracker, {person(0.5, 0.5), person(0.85)}, 0.6);
        nav.update(tracker, 0.8);
        for (int i = 0; i < 3; ++i) {
            const double now = 0.9 + i * 0.1;
            tracker.update({person(0.5, 0.7), person(0.85)}, now);
            require(stopped(nav.update(tracker, now)), "Close person should stop motors");
        }
        require(tracker.tracks()[0].visited && nav.status().find("Waiting") == 0,
                "Arrival should mark ID visited and start waiting");
        for (int i = 0; i < 99; ++i) {
            const double now = 1.2 + i * 0.1;
            tracker.update({person(0.5, 0.7), person(0.85)}, now);
            require(stopped(nav.update(tracker, now)), "Wait must keep both motors stopped for ten seconds");
        }
        tracker.update({person(0.5, 0.7), person(0.85)}, 11.11);
        command = nav.update(tracker, 11.11);
        require(nav.targetId() == 2 && command.left > 0 && command.right < 0,
                "After waiting choose unvisited visible ID and turn toward it");
        tracker.update({}, 11.2);
        require(stopped(nav.update(tracker, 11.2)), "Lost second target must stop");
        tracker.update({}, 13.3);
        require(stopped(nav.update(tracker, 13.3)), "Expired target should stop before searching");
        require(nav.targetId() == -1, "Expired target lock must clear");
        command = nav.update(tracker, 13.4);
        require(command.left > 0 && command.right < 0, "Resume search after target expires");
        confirm(tracker, {person(0.5)}, 13.5);
        require(tracker.tracks()[0].id == 3, "Expired IDs must never be recycled");

        PersonTracker visited;
        PersonNavigation searching;
        confirm(visited, {person(0.5)}, 0);
        visited.markVisited(1);
        searching.update(visited, 0.2);
        require(searching.targetId() == -1, "A visible visited ID must not be selected again");

        PersonTracker left;
        PersonNavigation turning;
        confirm(left, {person(0.1)}, 0);
        command = turning.update(left, 0.2);
        require(command.left < 0 && command.right > 0, "Person left of camera should cause left pivot");
        bool invalid = false;
        try { PersonNavigation bad(0.3, 0.22, 0); } catch (const std::invalid_argument&) { invalid = true; }
        require(invalid, "Invalid configuration must fail");
        std::cout << "Navigation tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
