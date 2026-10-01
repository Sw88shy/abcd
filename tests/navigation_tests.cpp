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
        for (int i = 0; i < 110; ++i) {
            const double now = 0.9 + i * 0.1;
            tracker.update({person(0.5, 0.7), person(0.85)}, now);
            command = nav.update(tracker, now);
            require(command.left == 0.3 && command.right == 0.3,
                    "Close target must retain forward power, without arrival or waiting");
        }
        require(!tracker.tracks()[0].visited && nav.targetId() == 1,
                "Continuous following must stay locked on the same ID");
        tracker.update({person(0.85)}, 12.0);
        require(stopped(nav.update(tracker, 12.0)), "Lost target must stop");
        confirm(tracker, {person(0.85)}, 14.1);
        require(stopped(nav.update(tracker, 14.3)), "Expired target should stop before selecting another");
        require(nav.targetId() == -1, "Expired target lock must clear");
        command = nav.update(tracker, 14.4);
        require(nav.targetId() == 3 && command.left > 0 && command.right > 0,
                "A new confirmed person should be followed after target loss");
        tracker.update({}, 16.5);
        nav.update(tracker, 16.5);
        command = nav.update(tracker, 16.6);
        require(command.left > 0 && command.right < 0, "Resume search after target expires");
        confirm(tracker, {person(0.5)}, 16.7);
        require(tracker.tracks()[0].id == 4, "Expired IDs must never be recycled");

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
        require(command.left > 0 && command.left < command.right,
                "Person left of camera should steer left while both tracks drive forward");

        PersonTracker close;
        PersonNavigation pausing;
        confirm(close, {person(0.5), person(0.85, 0.9)}, 0);
        command = pausing.update(close, 0.2);
        require(command.left > 0 && command.right > 0,
                "A close non-target person must not pause the selected target");
        close.update({person(0.5, 0.6), person(0.85, 0.9)}, 0.3);
        pausing.update(close, 0.3);
        close.update({person(0.5, 0.85), person(0.85, 0.9)}, 0.4);
        require(stopped(pausing.update(close, 0.4)), "Selected target entering close zone must stop");
        require(pausing.status().find("Pausing 5s") == 0, "Status must show five-second pause");
        for (int i = 1; i <= 49; ++i) {
            const double now = 0.4 + i * 0.1;
            close.update({person(0.5, 0.85), person(0.85, 0.9)}, now);
            require(stopped(pausing.update(close, now)), "Both motors must stay stopped for five seconds");
        }
        close.update({person(0.5, 0.85), person(0.85, 0.9)}, 5.4);
        command = pausing.update(close, 5.4);
        require(command.left > 0 && command.right > 0 && pausing.targetId() == 1,
                "At five seconds resume driving toward the same ID");
        close.update({person(0.5, 0.85), person(0.85, 0.9)}, 5.5);
        require(!stopped(pausing.update(close, 5.5)), "Close box must not immediately retrigger pause");
        close.update({person(0.5, 0.6), person(0.85, 0.9)}, 5.6);
        pausing.update(close, 5.6);
        close.update({person(0.5, 0.85), person(0.85, 0.9)}, 5.7);
        require(stopped(pausing.update(close, 5.7)), "Moving away then returning must rearm pause");
        close.update({}, 8.0);
        require(stopped(pausing.update(close, 8.0)), "Target expiry must not interrupt the five-second pause");
        require(stopped(pausing.update(close, 10.7)), "Expired target must not be driven toward after pause");
        command = pausing.update(close, 10.8);
        require(command.left > 0 && command.right < 0, "After pause with expired target resume searching");

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
