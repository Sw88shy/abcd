#pragma once

#include <net.h>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

struct Detection {
    cv::Rect box;
    float confidence;
};

class YoloDetector {
public:
    YoloDetector(const std::string& directory, int threads);
    std::vector<Detection> detect(const cv::Mat& frame, float confidence, float iou);
private:
    ncnn::Net net_;
    int inputSize_ = 320;
};
