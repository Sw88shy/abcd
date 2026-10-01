# USB camera people detector for Raspberry Pi

C++17 application that captures a USB webcam, detects people, and draws green
bounding boxes with a live person count. Uses OpenCV's built-in HOG/SVM detector:
no model download or accelerator is required.

## Build on Raspberry Pi OS

Copy this folder onto your Pi, connect your USB webcam, and run these commands
from the folder:

```bash
sudo apt update
sudo apt install -y build-essential cmake libopencv-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/people_detector
```

The default camera is index 0 (`/dev/video0` on Linux). The preview needs a
desktop display on the Pi. Press **Q**, **Escape**, or **Ctrl+C** to stop.

## Options

OpenCV command-line options use `--name=value`:

```bash
./build/people_detector --help
./build/people_detector --camera=1
./build/people_detector --width=640 --height=480 --detect-width=320
./build/people_detector --threshold=0.5
./build/people_detector --output=people.avi
./build/people_detector --headless=true --output=people.avi
```

Headless mode works without a preview (for example over SSH). It still detects
people, prints count changes, and draws boxes in the optional recorded video.
Ctrl+C stops recording and closes the file. The output is MJPEG in an AVI
container; use an `.avi` filename. Existing output files are overwritten.
`--fps=15` requests camera FPS and sets recording playback FPS. Camera requests
are best effort. Recording stores each processed frame, so playback may be
faster than real time if detection runs below the requested FPS.

## Detection quality and speed

This detector works best for upright people whose full bodies are visible. It
can miss seated, nearby, small, or partly hidden people and produce false
positives. It does not recognize identities or track individuals between frames.
Actual speed depends on the Pi model, camera, and scene. Start at the default
480-pixel detection width; try 320 for speed or 640 for smaller distant people.
The resized image must be at least 64 by 128 pixels. A higher threshold reduces
false positives but can miss more people. The preview reports processing FPS,
including capture and detection, before display/recording overhead.

## Troubleshooting

- List camera devices with `ls -l /dev/video*`, or install `v4l-utils` and run
  `v4l2-ctl --list-devices`. Try the index matching your USB camera's capture node.
- Close other applications using the webcam. Verify the USB cable and power.
- If camera access is denied, check membership with `groups`. On Raspberry Pi OS,
  add your account with `sudo usermod -aG video "$USER"`, then log out and back in.
- If no preview display is available, use `--headless=true --output=people.avi`.

Based on the APIs demonstrated in the official
[OpenCV people detection sample](https://docs.opencv.org/4.10.0/df/d54/samples_2cpp_2peopledetect_8cpp-example.html).
