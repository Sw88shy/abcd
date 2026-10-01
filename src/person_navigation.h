#pragma once

#include <string>
#include <vector>

// Boxes are normalised to the camera image, independent of OpenCV.
struct PersonBox {
    double x, y, width, height, confidence;
};

struct PersonTrack {
    int id;
    PersonBox box;
    double lastSeen;
    int hits = 1;
    bool visible = true;
    bool visited = false;
};

class PersonTracker {
public:
    void update(const std::vector<PersonBox>& detections, double now);
    const std::vector<PersonTrack>& tracks() const { return tracks_; }
    void markVisited(int id);
private:
    std::vector<PersonTrack> tracks_;
    int nextId_ = 1;
};

struct DriveCommand { double left = 0, right = 0; };

class PersonNavigation {
public:
    PersonNavigation(double speed = 0.30, double turnSpeed = 0.22,
                     double stopHeight = 0.80);
    DriveCommand update(PersonTracker& tracker, double now);
    std::string status() const;
    int targetId() const { return targetId_; }
private:
    enum class State { Search, Approach, Wait };
    State state_ = State::Search;
    int targetId_ = -1;
    double waitUntil_ = 0;
    bool pauseArmed_ = true;
    double speed_, turnSpeed_, stopHeight_;
};
