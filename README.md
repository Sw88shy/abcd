# Raspberry Pi 5 person-following tank with L298N

C++17 app using **YOLO26n + NCNN on the CPU**, with OpenCV for USB camera
capture and preview. Draws green boxes, confidence scores, a count, and processing
FPS. No AI accelerator is required. Detects the COCO person class (class 0).
Adds temporary person IDs and optional two-motor tank control.

## Install on 64-bit Raspberry Pi OS

Run these commands from this project folder:

```bash
sudo apt update
sudo apt install -y build-essential cmake git libopencv-dev python3-venv
git clone --depth 1 https://github.com/Tencent/ncnn.git third_party/ncnn
cmake -S third_party/ncnn -B build-ncnn \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/third_party/ncnn-install" \
  -DNCNN_VULKAN=OFF -DNCNN_BUILD_TOOLS=OFF \
  -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BUILD_BENCHMARK=OFF
cmake --build build-ncnn -j2
cmake --install build-ncnn
```

## Download and export the model

Weights are downloaded during export, not bundled in this repository. Internet
is needed for installation and the first export. Python is only needed for this
preparation; the camera application itself is C++.

```bash
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install ultralytics ncnn pnnx==20260526
.venv/bin/python scripts/export_model.py --size=320
```

The script downloads `yolo26n.pt`, exports NCNN, runs dummy inference, and validates
the output layout. It creates `models/yolo26n_ncnn_model/` containing
`model.ncnn.param`, `model.ncnn.bin`, and `input-size.txt`. Keep these together.
PNNX is pinned to match the current official Ultralytics exporter workaround.

Export takes time and memory on the Pi. Alternatively, run the exporter on a
desktop with these Python packages installed and copy the complete exported
directory to `models/yolo26n_ncnn_model/` on the Pi.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/third_party/ncnn-install"
cmake --build build -j2
./build/people_detector
```

Run from the project folder so the default model path resolves. Default camera
index is 0 (`/dev/video0`). The preview requires a desktop session. Press **Q**,
**Escape**, or **Ctrl+C** to stop.

## Accuracy, speed, and options

Default input is **320 × 320**, with four CPU inference threads. For smaller or
distant people, export a **640 × 640** model, which requires more processing:

```bash
.venv/bin/python scripts/export_model.py --size=640 --destination=models/yolo26n_640_ncnn_model
./build/people_detector --model=models/yolo26n_640_ncnn_model
```

Try `--threshold=0.25` to detect more people or `--threshold=0.5` to reduce false
positives. Default confidence is 0.35; overlap suppression defaults to
`--iou=0.45`. It can still miss people or produce false positives. Temporary
tracking IDs do not recognise real-world identities.

Options use `--name=value`:

```bash
./build/people_detector --help
./build/people_detector --camera=1 --width=1280 --height=720
./build/people_detector --threads=3 --threshold=0.25
./build/people_detector --output=people.avi
./build/people_detector --headless=true --output=people.avi
```

Camera resolution and FPS requests are best effort. Model input size is read
from `input-size.txt`: re-export to change it, rather than editing this file.
Speed depends on the Pi, cooling, camera, and input size. Displayed processing
FPS includes capture/detection before display and recording overhead.

Headless mode works over SSH, prints count changes, and optionally records
annotated frames. Recording uses MJPEG AVI and overwrites existing output files.
Ctrl+C closes the video. `--fps=15` requests camera FPS and sets output playback
FPS. If inference is slower than that rate, playback runs faster than real time.

## Troubleshooting

- **Model missing:** run the exporter or pass `--model=/full/path/to/model`.
- **Unexpected output:** use the provided exporter with the 80-class COCO
  YOLO26n detection model; custom-class, pose, and segmentation models differ.
- **NCNN missing in CMake:** pass the installation prefix shown above.
- **Camera missing:** inspect `ls -l /dev/video*`. With `v4l-utils` installed,
  `v4l2-ctl --list-devices` identifies the webcam's capture node. Close other
  applications using the camera.
- **Camera permission denied:** inspect `groups`. Add your account with
  `sudo usermod -aG video "$USER"`, then log out and back in.
- **No display:** use `--headless=true --output=people.avi`.

## Implementation and verification

Preprocessing preserves aspect ratio, pads with 114, converts BGR to RGB, and
normalizes to [0,1]. NCNN exports raw one-to-many predictions: this app selects
the highest-scoring person class, filters confidence, removes duplicates with
NMS, and maps boxes back to the camera image. Output dimensions are checked
against the export size.

The exporter checks model loading and output shape before marking the model
ready. The Windows development machine lacks OpenCV/NCNN development libraries,
so native compilation, actual model inference, camera accuracy, and Pi FPS still
need verification on the target Pi.

References:
- [NCNN export](https://docs.ultralytics.com/integrations/ncnn/)
- [YOLO26 output layouts](https://docs.ultralytics.com/guides/end2end-detection/)
- [Official exporter and PNNX pin](https://github.com/ultralytics/ultralytics/blob/main/ultralytics/utils/export/ncnn.py)
- [NCNN source](https://github.com/Tencent/ncnn)

Ultralytics weights/tools use AGPL-3.0 or an Enterprise license; see
[Ultralytics licensing](https://www.ultralytics.com/license).

## Person-following behaviour

The tank pivots to find people, locks onto the first ID confirmed over three
fresh frames, and continuously drives toward that person. If several arrive
together, the detector's confidence order decides which ID is first. Both tracks
run forward during following; steering reduces the inside track by at most 25%.
The outside track uses the full `--speed` duty, with no reduction based on person
size. There is no arrival stop, ten-second wait, or automatic handoff on arrival.
It stays locked on the chosen ID until that track expires.

IDs use bounding-box overlap and position, not face or appearance recognition.
Brief misses retain an ID for two seconds; a missing target stops motion
immediately and must be confirmed again before motion resumes. After expiration,
the tank chooses another confirmed visible person, or pivots to search. IDs are
never reused within a run. Someone who leaves view and returns can receive a new
ID. Crossing people, occlusion, fast turns, or poor detections can swap IDs.

`--stop-height` is accepted for command-line compatibility but has no effect.
There is no obstacle detection or proximity stop: the tank continues forward
even when very close to its target. Target loss, processing timeouts, program
exit and GPIO errors still disable the motors. Disabling the bridge coasts.

## Wire the L298N to the Pi

Power off the Pi and motor supply before wiring. Numbers below are BCM GPIO
numbers and physical positions on the Pi's **40-pin header**, including Pi 5.
The camera should face forward between the tracks.

| L298N terminal | Pi GPIO | Physical header pin | Purpose |
| --- | --- | --- | --- |
| IN1 | GPIO17 | 11 | Left direction |
| IN2 | GPIO27 | 13 | Left direction |
| ENA | GPIO18 | 12 | Left speed (software PWM) |
| IN3 | GPIO23 | 16 | Right direction |
| IN4 | GPIO24 | 18 | Right direction |
| ENB | GPIO13 | 33 | Right speed (software PWM) |
| GND | Ground | 6 | Shared ground |

- Remove the **ENA and ENB jumpers** so the Pi can control speed and disable the
  motors. Add a 10 kΩ pull-down resistor from each enable terminal to GND to keep
  motors disabled before the app starts and after GPIO is released.
- Connect the **left motor** to OUT1/OUT2 and the **right motor** to OUT3/OUT4.
- Connect a separate motor supply's positive terminal to the driver's motor
  supply terminal (often labelled `12V`, `VS`, or `VMS`), and its negative to GND.
  Choose the voltage/current for your motors and module, allowing for L298N
  voltage drop and motor stall current. Do not power the motors from Pi GPIO,
  3.3V, or the Pi's 5V header. Power the Pi with its normal supply.
- L298N logic needs 5V. On typical modules with a **5V-EN regulator jumper**,
  removing that jumper allows a separate regulated 5V supply at the `5V` logic
  terminal. Connect that supply's ground to the same GND. Confirm this against
  your module's instructions; regulator jumper/terminal behaviour varies.
  If instead using the module's onboard regulator, follow its permitted motor
  supply range and leave its 5V terminal disconnected from the Pi.
- Do not connect the driver's 5V terminal to a Pi GPIO or join its regulator
  output to the Pi's 5V rail. Pi GPIO uses 3.3V; the L298 input-high threshold is
  2.3V, so the Pi can drive the six logic inputs directly.

Sources: [Pi GPIO documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html),
[ST L298 datasheet](https://www.st.com/resource/en/datasheet/l298.pdf).

## Build and run with motors

Motor GPIO support uses **lgpio**, compatible with Pi 5's GPIO chip interface.
The default build/run previews tracking and does not request GPIO lines.
To build with motor support:

```bash
sudo apt install -y liblgpio-dev gpiod
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/third_party/ncnn-install" -DENABLE_MOTORS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
gpiodetect
./build/people_detector
```

In `gpiodetect`, find the chip labelled **pinctrl-rp1** on Pi 5. Current Raspberry
Pi OS normally uses gpiochip0; older kernels may use gpiochip4. Pass its number
using `--gpiochip=4` if needed. Select the header chip, not another GPIO device.
Your account needs access to `/dev/gpiochipN` (normally the `gpio` group on
Raspberry Pi OS). If required, add it with `sudo usermod -aG gpio "$USER"` and
log out/in; do not run the whole app as root just to access GPIO.

First test with the tracks lifted clear of the floor. Check preview IDs and motor
direction, then enable driving:

```bash
./build/people_detector --drive=true --gpiochip=0
./build/people_detector --drive=true --headless=true
```

For more starting torque during continuous following, try
`./build/people_detector --drive=true --speed=0.75`. This supplies 75% duty on
both tracks when the target is centred, and at least 56.25% on the inside track
when steering. Actual wheel speed depends on the motors, supply and load.

If one motor runs backward, swap that motor's two OUT wires with power off, or
use `--invert-left=true` / `--invert-right=true`. Options `--speed=0.30` and
`--turn-speed=0.22` control PWM duty, not measured wheel speed. Adjust for the
motor's starting torque and your gearing.

GPIO outputs initialise low. Q/Escape, Ctrl+C, normal exits and exceptions disable
both motors. An independent watchdog disables them after 0.75 seconds without a
new command, including a blocked capture/inference. A frame taking longer than
that is discarded for navigation and logs a stopped status. If your Pi needs
longer, tune `--motor-timeout=1.5`; this also increases the maximum delay before
a stall stops movement. This is a software timeout, not a hardware emergency
stop, and does not cover power loss or an unresponsive OS. lgpio produces 100 Hz
software PWM on ENA/ENB.

The Windows development machine cannot verify physical motor operation or build
the full app without OpenCV/NCNN. Verify these on the target Pi. The navigation
tests check ID retention, target loss, continuous forward steering, continued
motion at close range, and selecting another visible person after target loss. Simulated GPIO tests
check startup, PWM duty, polarity reversal, timeout shutdown, cleanup and GPIO
failures; they cannot validate electrical operation. Run tests without
the camera dependencies:

```bash
cmake -S . -B build-tests -DBUILD_DETECTOR=OFF
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

GPIO implementation follows the [lgpio C API](https://github.com/joan2937/lg/blob/master/lgpio.h).
Chip selection follows [Raspberry Pi GPIO best practices](https://pip-assets.raspberrypi.com/categories/685-app-notes-guides-whitepapers/documents/RP-006553-WP/A-history-of-GPIO-usage-on-Raspberry-Pi-devices-and-current-best-practices).
