#include "yolo_detector.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

YoloDetector::YoloDetector(const std::string& directory, int threads) {
    std::ifstream config(directory + "/input-size.txt");
    if (!(config >> inputSize_) || inputSize_ < 32 || inputSize_ > 1280 || inputSize_ % 32 != 0) {
        throw std::runtime_error("Missing or invalid input-size.txt. Run scripts/export_model.py first.");
    }
    net_.opt.use_vulkan_compute = false;
    net_.opt.num_threads = threads;
    // Keep extracted tensors as unpacked FP32 for decoding.
    net_.opt.use_packing_layout = false;
    net_.opt.use_fp16_storage = false;
    net_.opt.use_fp16_arithmetic = false;
    if (net_.load_param((directory + "/model.ncnn.param").c_str()) != 0 ||
        net_.load_model((directory + "/model.ncnn.bin").c_str()) != 0) {
        throw std::runtime_error("Cannot load NCNN model from " + directory);
    }
    if (net_.input_names().size() != 1 || net_.output_names().size() != 1) {
        throw std::runtime_error("Expected a YOLO26n detection model with one input and output.");
    }
}

std::vector<Detection> YoloDetector::detect(const cv::Mat& frame, float confidence, float iou) {
    const double scale = std::min(static_cast<double>(inputSize_) / frame.cols,
                                  static_cast<double>(inputSize_) / frame.rows);
    const int resizedWidth = std::clamp(cvRound(frame.cols * scale), 1, inputSize_);
    const int resizedHeight = std::clamp(cvRound(frame.rows * scale), 1, inputSize_);
    const int left = (inputSize_ - resizedWidth) / 2;
    const int top = (inputSize_ - resizedHeight) / 2;
    cv::Mat resized, padded;
    cv::resize(frame, resized, cv::Size(resizedWidth, resizedHeight), 0, 0, cv::INTER_LINEAR);
    cv::copyMakeBorder(resized, padded, top, inputSize_ - resizedHeight - top,
                       left, inputSize_ - resizedWidth - left, cv::BORDER_CONSTANT,
                       cv::Scalar(114, 114, 114));
    ncnn::Mat input = ncnn::Mat::from_pixels(padded.data, ncnn::Mat::PIXEL_BGR2RGB,
                                            inputSize_, inputSize_);
    const float normalization[3] = {1.f / 255.f, 1.f / 255.f, 1.f / 255.f};
    input.substract_mean_normalize(nullptr, normalization);
    auto extractor = net_.create_extractor();
    ncnn::Mat output;
    if (extractor.input(net_.input_names()[0], input) != 0 ||
        extractor.extract(net_.output_names()[0], output) != 0) {
        throw std::runtime_error("NCNN inference failed.");
    }
    // COCO raw output: 4 decoded xywh coordinates + 80 class probabilities.
    // PNNX can emit either [84, candidates] or [candidates, 84].
    if (output.dims == 3 && output.c == 1) output = output.reshape(output.w, output.h);
    const int candidates = 21 * (inputSize_ / 32) * (inputSize_ / 32);
    const bool channelsFirst = output.dims == 2 && output.h == 84 && output.w == candidates;
    const bool channelsLast = output.dims == 2 && output.w == 84 && output.h == candidates;
    if ((!channelsFirst && !channelsLast) || output.elempack != 1 || output.elemsize != sizeof(float)) {
        throw std::runtime_error("Unexpected output shape/type. Export the COCO YOLO26n detection model "
                                 "using scripts/export_model.py; input size must match the export.");
    }
    const auto value = [&](int candidate, int channel) {
        return channelsFirst ? output.row(channel)[candidate] : output.row(candidate)[channel];
    };
    std::vector<Detection> proposals;
    for (int i = 0; i < candidates; ++i) {
        const float score = value(i, 4);  // COCO class 0: person; no separate objectness.
        if (!std::isfinite(score) || score < confidence || score > 1.f) continue;
        // Match single-label YOLO postprocessing: the highest scoring class must be person.
        bool person = true;
        for (int c = 5; c < 84; ++c) {
            if (value(i, c) > score) { person = false; break; }
        }
        if (!person) continue;
        const float cx = value(i, 0), cy = value(i, 1), w = value(i, 2), h = value(i, 3);
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(w) ||
            !std::isfinite(h) || w <= 0 || h <= 0) continue;
        const auto x = [&](double v) { return std::clamp((v - left) / scale, 0.0, double(frame.cols)); };
        const auto y = [&](double v) { return std::clamp((v - top) / scale, 0.0, double(frame.rows)); };
        const int x1 = static_cast<int>(std::floor(x(cx - w * 0.5)));
        const int y1 = static_cast<int>(std::floor(y(cy - h * 0.5)));
        const int x2 = static_cast<int>(std::ceil(x(cx + w * 0.5)));
        const int y2 = static_cast<int>(std::ceil(y(cy + h * 0.5)));
        if (x2 > x1 && y2 > y1) proposals.push_back({cv::Rect(x1, y1, x2 - x1, y2 - y1), score});
    }
    std::sort(proposals.begin(), proposals.end(), [](const Detection& a, const Detection& b) {
        return a.confidence > b.confidence;
    });
    std::vector<Detection> detections;
    for (const auto& candidate : proposals) {
        bool suppressed = false;
        for (const auto& selected : detections) {
            const double intersection = (candidate.box & selected.box).area();
            const double unionArea = double(candidate.box.area()) + selected.box.area() - intersection;
            if (unionArea > 0 && intersection / unionArea > iou) { suppressed = true; break; }
        }
        if (!suppressed) detections.push_back(candidate);
        if (detections.size() == 300) break;
    }
    return detections;
}
