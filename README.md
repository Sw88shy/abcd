# Raspberry Pi 5 USB camera person detection

C++17 app using **YOLO26n + NCNN on the CPU**, with OpenCV for USB camera
capture and preview. Draws green boxes, confidence scores, a count, and processing
FPS. No AI accelerator is required. Detects the COCO person class (class 0).

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
`--iou=0.45`. It can still miss people or produce false positives; it does not
recognize identities or track individuals.

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
