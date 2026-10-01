#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include "yolo_detector.h"
#include "person_navigation.h"
#include "motor_driver.h"
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
constexpr const char* window = "USB camera - people detection";
}

int main(int argc, char** argv) {
    const cv::String keys =
        "{help h usage ? | | Show help}"
        "{camera c |0| USB camera index (0 means /dev/video0 on Linux)}"
        "{width |640| Requested camera width}"
        "{height |480| Requested camera height}"
        "{model |models/yolo26n_ncnn_model| Exported NCNN model directory}"
        "{threads |4| CPU inference threads}"
        "{threshold |0.35| Person confidence threshold (0 to 1)}"
        "{iou |0.45| Overlap suppression threshold (0 to 1)}"
        "{headless |false| Disable preview window}"
        "{output | | Optional annotated MJPEG AVI file}"
        "{fps |15| Requested camera FPS and output playback FPS}"
        "{drive |false| Enable real L298N motor movement}"
        "{gpiochip |0| GPIO header chip number (check gpiodetect on Pi)}"
        "{speed |0.30| Maximum forward motor duty (0 to 1)}"
        "{turn-speed |0.22| Pivot motor duty (0 to 1)}"
        "{stop-height |0.65| Stop at this fraction of camera height}"
        "{motor-timeout |0.75| Stop motors after this many seconds without fresh processing}"
        "{invert-left |false| Reverse left motor polarity}"
        "{invert-right |false| Reverse right motor polarity}";
    cv::CommandLineParser parser(argc, argv, keys);
    parser.about("USB camera people detector (YOLO26n + NCNN, CPU)");
    if (parser.has("help")) {
        parser.printMessage();
        return 0;
    }
    const int camera = parser.get<int>("camera");
    const int width = parser.get<int>("width");
    const int height = parser.get<int>("height");
    const std::string model = parser.get<std::string>("model");
    const int threads = parser.get<int>("threads");
    const double iou = parser.get<double>("iou");
    const double threshold = parser.get<double>("threshold");
    const double fps = parser.get<double>("fps");
    const bool headless = parser.get<bool>("headless");
    const std::string output = parser.get<std::string>("output");
    const bool drive = parser.get<bool>("drive");
    const int gpiochip = parser.get<int>("gpiochip");
    const double speed = parser.get<double>("speed");
    const double turnSpeed = parser.get<double>("turn-speed");
    const double stopHeight = parser.get<double>("stop-height");
    const double motorTimeout = parser.get<double>("motor-timeout");
    const bool invertLeft = parser.get<bool>("invert-left");
    const bool invertRight = parser.get<bool>("invert-right");
    if (!parser.check()) {
        parser.printErrors();
        return 1;
    }
    if (camera < 0 || width < 1 || height < 1 || threads < 1 || threads > 64 || model.empty() ||
        !std::isfinite(fps) || fps <= 0 || !std::isfinite(threshold) || threshold <= 0 || threshold > 1 ||
        !std::isfinite(iou) || iou <= 0 || iou > 1) {
        std::cerr << "Invalid options: positive dimensions/FPS, camera >= 0, threads 1..64, "
                     "and threshold/iou in (0, 1] are required.\n";
        return 1;
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    try {
        PersonTracker tracker;
        PersonNavigation navigation(speed, turnSpeed, stopHeight);
        if (gpiochip < 0 || !std::isfinite(motorTimeout) || motorTimeout <= 0)
            throw std::invalid_argument("gpiochip must be >= 0 and motor-timeout must be positive.");
        YoloDetector detector(model, threads);
        cv::VideoCapture capture;
#ifdef __linux__
        capture.open(camera, cv::CAP_V4L2);
#else
        capture.open(camera, cv::CAP_ANY);
#endif
        if (!capture.isOpened()) {
            std::cerr << "Cannot open camera " << camera
                      << ". Check the USB connection, camera index, permissions, "
                         "and whether another app is using it.\n";
            return 1;
        }
        capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        capture.set(cv::CAP_PROP_FPS, fps);
        capture.set(cv::CAP_PROP_BUFFERSIZE, 1);

        cv::VideoWriter writer;
        if (!headless) cv::namedWindow(window, cv::WINDOW_NORMAL);
        MotorDriver motors(drive, gpiochip, invertLeft, invertRight, motorTimeout, &stopped);
        std::cout << (drive ? "Motor movement ENABLED.\n" : "Tracking preview: motors disabled (--drive=true to enable).\n");
        std::cout << "Camera opened. Press Q or Escape in the preview, or Ctrl+C to stop.\n";
        int previousCount = -1;
        std::string previousState;
        while (!stopped) {
            const auto start = cv::getTickCount();
            const auto frameStart = std::chrono::steady_clock::now();
            cv::Mat frame;
            if (!capture.read(frame) || frame.empty()) {
                std::cerr << "Camera stopped delivering frames.\n";
                return 1;
            }
            const auto boxes = detector.detect(frame, static_cast<float>(threshold), static_cast<float>(iou));
            if (stopped) break;
            const auto processed = std::chrono::steady_clock::now();
            const double now = std::chrono::duration<double>(processed.time_since_epoch()).count();
            const bool fresh = std::chrono::duration<double>(processed - frameStart).count() <= motorTimeout;
            std::vector<PersonBox> people;
            if (fresh) {
                for (const auto& box : boxes) {
                    people.push_back({double(box.box.x) / frame.cols, double(box.box.y) / frame.rows,
                                      double(box.box.width) / frame.cols, double(box.box.height) / frame.rows,
                                      box.confidence});
                }
            }
            tracker.update(people, now);
            // A slow frame cannot restart motion after the watchdog has stopped it.
            const DriveCommand command = fresh ? navigation.update(tracker, now) : DriveCommand{};
            motors.set(command);
            const std::string navStatus = fresh ? navigation.status() : "Stopped: camera processing too slow";
            if (navStatus != previousState) {
                std::cout << navStatus << std::endl;
                previousState = navStatus;
            }
            for (const auto& track : tracker.tracks()) {
                if (!track.visible) continue;
                const cv::Rect original(cvRound(track.box.x * frame.cols), cvRound(track.box.y * frame.rows),
                                        cvRound(track.box.width * frame.cols), cvRound(track.box.height * frame.rows));
                const cv::Scalar colour = track.visited ? cv::Scalar(160, 160, 160) :
                    track.id == navigation.targetId() ? cv::Scalar(0, 200, 255) : cv::Scalar(0, 255, 0);
                cv::rectangle(frame, original, colour, 2);
                std::ostringstream label;
                label << "ID " << track.id << (track.visited ? " visited " : " ")
                      << std::fixed << std::setprecision(2) << track.box.confidence;
                cv::putText(frame, label.str(), cv::Point(original.x, std::max(18, original.y - 6)),
                            cv::FONT_HERSHEY_SIMPLEX, 0.55, colour, 2);
            }
            const double seconds = (cv::getTickCount() - start) / cv::getTickFrequency();
            std::ostringstream status;
            status << "People: " << boxes.size() << " | Processing FPS: "
                   << std::fixed << std::setprecision(1) << 1.0 / std::max(seconds, 1e-9);
            cv::rectangle(frame, cv::Rect(0, 0, frame.cols, std::min(62, frame.rows)), cv::Scalar(0, 0, 0), cv::FILLED);
            cv::putText(frame, status.str(), cv::Point(8, 23), cv::FONT_HERSHEY_SIMPLEX,
                        0.55, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            cv::putText(frame, (drive ? "DRIVE: " : "PREVIEW: ") + navStatus, cv::Point(8, 48),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            if (previousCount != static_cast<int>(boxes.size())) {
                previousCount = static_cast<int>(boxes.size());
                std::cout << "People detected: " << previousCount << std::endl;
            }
            if (!output.empty()) {
                if (!writer.isOpened()) {
                    if (!writer.open(output, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, frame.size())) {
                        std::cerr << "Cannot create output video: " << output << '\n';
                        return 1;
                    }
                }
                writer.write(frame);
            }
            if (!headless) {
                cv::imshow(window, frame);
                const int key = cv::waitKey(1) & 0xff;
                if (key == 'q' || key == 'Q' || key == 27 ||
                    cv::getWindowProperty(window, cv::WND_PROP_VISIBLE) < 1) break;
            }
        }
        motors.set({});
        return 0;
    } catch (const cv::Exception& error) {
        std::cerr << "OpenCV error: " << error.what()
                  << "\nFor a Pi without a desktop display, use --headless=true --output=people.avi.\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
