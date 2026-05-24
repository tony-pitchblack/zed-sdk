///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2025, STEREOLABS.
//
// All rights reserved.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////

// Standard includes
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// ZED includes
#include <sl/Camera.hpp>
#include <sl/CameraOne.hpp>

// Sample includes
#include "utils.hpp"

struct AppOptions {
    bool record_sensors = false;
    int open_timeout_sec = 15;
    int enumeration_timeout_sec = 15;
    int enumeration_min_cam = 2;
};

template <typename CameraType>
void acquisition(CameraType& zed) {
    auto infos = zed.getCameraInformation();

    while (!exit_app) {
        if (zed.grab() <= sl::ERROR_CODE::SUCCESS) {
        }
    }

    std::cout << infos.camera_model << "[" << infos.serial_number << "] QUIT \n";

    zed.disableRecording();
    zed.close();
}

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog
              << " [--record-sensors] [--open-timeout-sec <seconds>] [--enumeration-timeout-sec <seconds>] [--enumeration-min-cam <count>]\n";
    std::cout << "  --record-sensors                      Require motion sensors and include native sensor metadata in SVO\n";
    std::cout << "  --open-timeout-sec <seconds>          Retry camera open once per second. Default: 15\n";
    std::cout << "  --enumeration-timeout-sec <seconds>   Retry device enumeration once per second. Default: 15\n";
    std::cout << "  --enumeration-min-cam <count>         Minimum camera count required before startup. Default: 2\n";
    std::cout << "  -h, --help                            Show this help\n";
}

enum class ParseResult { Ok, Help, Error };

ParseResult parseArgs(int argc, char** argv, AppOptions& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--record-sensors") {
            opts.record_sensors = true;
        } else if (arg == "--open-timeout-sec") {
            if (i + 1 >= argc) {
                std::cout << "Missing value for --open-timeout-sec" << std::endl;
                printUsage(argv[0]);
                return ParseResult::Error;
            }
            char* end = nullptr;
            const long timeout = std::strtol(argv[++i], &end, 10);
            if (*end != '\0' || timeout <= 0) {
                std::cout << "Invalid --open-timeout-sec value: " << argv[i] << std::endl;
                printUsage(argv[0]);
                return ParseResult::Error;
            }
            opts.open_timeout_sec = static_cast<int>(timeout);
        } else if (arg == "--enumeration-timeout-sec") {
            if (i + 1 >= argc) {
                std::cout << "Missing value for --enumeration-timeout-sec" << std::endl;
                printUsage(argv[0]);
                return ParseResult::Error;
            }
            char* end = nullptr;
            const long timeout = std::strtol(argv[++i], &end, 10);
            if (*end != '\0' || timeout <= 0) {
                std::cout << "Invalid --enumeration-timeout-sec value: " << argv[i] << std::endl;
                printUsage(argv[0]);
                return ParseResult::Error;
            }
            opts.enumeration_timeout_sec = static_cast<int>(timeout);
        } else if (arg == "--enumeration-min-cam") {
            if (i + 1 >= argc) {
                std::cout << "Missing value for --enumeration-min-cam" << std::endl;
                printUsage(argv[0]);
                return ParseResult::Error;
            }
            char* end = nullptr;
            const long min_count = std::strtol(argv[++i], &end, 10);
            if (*end != '\0' || min_count <= 0) {
                std::cout << "Invalid --enumeration-min-cam value: " << argv[i] << std::endl;
                printUsage(argv[0]);
                return ParseResult::Error;
            }
            opts.enumeration_min_cam = static_cast<int>(min_count);
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return ParseResult::Help;
        } else {
            std::cout << "Unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            return ParseResult::Error;
        }
    }
    return ParseResult::Ok;
}

bool validateStereoSensors(sl::Camera& zed, const int sn) {
    sl::SensorsData sensors_data;
    for (int attempt = 0; attempt < 50; ++attempt) {
        const sl::ERROR_CODE err = zed.getSensorsData(sensors_data, sl::TIME_REFERENCE::CURRENT);
        if (err <= sl::ERROR_CODE::SUCCESS && sensors_data.imu.is_available) {
            std::cout << "ZED SN:" << sn << " Sensors validated" << std::endl;
            return true;
        }
        sl::sleep_ms(100);
    }
    std::cout << "ZED SN:" << sn << " Sensor validation failed" << std::endl;
    return false;
}

template <typename CameraType, typename InitParametersType>
sl::ERROR_CODE openCameraWithRetry(CameraType& zed, const InitParametersType& init_parameters, const int timeout_sec) {
    sl::ERROR_CODE open_err = sl::ERROR_CODE::CAMERA_NOT_DETECTED;
    for (int attempt = 0; attempt < timeout_sec && !exit_app; ++attempt) {
        open_err = zed.open(init_parameters);
        if (open_err <= sl::ERROR_CODE::SUCCESS) {
            return open_err;
        }
        zed.close();
        if (attempt + 1 < timeout_sec && !exit_app) {
            sl::sleep_ms(1000);
        }
    }
    return open_err;
}

template <typename CameraType>
bool startRecording(CameraType& zed, const int sn) {
    sl::RecordingParameters recording_params;
    std::string svo_filename = std::string(sl::toString(zed.getCameraInformation().camera_model)) + "_SN" + std::to_string(sn) + ".svo2";
    svo_filename.erase(std::remove(svo_filename.begin(), svo_filename.end(), ' '), svo_filename.end());
    recording_params.video_filename.set(svo_filename.c_str());
    recording_params.compression_mode = sl::SVO_COMPRESSION_MODE::H265;
    const sl::ERROR_CODE recording_err = zed.enableRecording(recording_params);
    if (recording_err <= sl::ERROR_CODE::SUCCESS) {
        std::cout << toString(zed.getCameraInformation().camera_model) << "_SN" << sn << " Enabled recording" << std::endl;
    } else {
        std::cout << "ZED SN:" << sn << " Recording initialization error: " << recording_err << std::endl;
        return false;
    }

    std::cout << "Recording SVO " << recording_params.video_filename << std::endl;
    return true;
}

bool openStereoCamera(sl::Camera& zed, const int sn, const int camera_fps, const bool record_sensors, const int open_timeout_sec) {
    sl::InitParameters init_parameters;
    init_parameters.camera_resolution = sl::RESOLUTION::AUTO;
    init_parameters.depth_mode = sl::DEPTH_MODE::NONE;
    init_parameters.input.setFromSerialNumber(sn);
    init_parameters.camera_fps = camera_fps;
    init_parameters.sensors_required = record_sensors;
    init_parameters.open_timeout_sec = 1.0f;

    const sl::ERROR_CODE open_err = openCameraWithRetry(zed, init_parameters, open_timeout_sec);
    if (open_err <= sl::ERROR_CODE::SUCCESS) {
        std::cout << toString(zed.getCameraInformation().camera_model) << "_SN" << sn << " Opened" << std::endl;
    } else {
        std::cout << "ZED SN:" << sn << " Error: " << open_err << std::endl;
        zed.close();
        return false;
    }

    if (record_sensors && !validateStereoSensors(zed, sn)) {
        zed.close();
        return false;
    }

    return true;
}

bool openOneCamera(sl::CameraOne& zed, const int sn, const int camera_fps, const int open_timeout_sec) {
    sl::InitParametersOne init_parameters;
    init_parameters.camera_resolution = sl::RESOLUTION::AUTO;
    init_parameters.input.setFromSerialNumber(sn);
    init_parameters.camera_fps = camera_fps;

    const sl::ERROR_CODE open_err = openCameraWithRetry(zed, init_parameters, open_timeout_sec);
    if (open_err <= sl::ERROR_CODE::SUCCESS) {
        std::cout << toString(zed.getCameraInformation().camera_model) << "_SN" << sn << " Opened" << std::endl;
    } else {
        std::cout << "ZED SN:" << sn << " Error: " << open_err << std::endl;
        zed.close();
        return false;
    }

    return true;
}

template <typename CameraType>
void closeOpenedCameras(std::vector<CameraType>& zeds) {
    for (auto& zed : zeds)
        if (zed.isOpened())
            zed.close();
}

template <typename CameraType>
void stopRecordingAndClose(std::vector<CameraType>& zeds) {
    for (auto& zed : zeds) {
        if (zed.isOpened()) {
            zed.disableRecording();
            zed.close();
        }
    }
}

void printDeviceInfo(const std::vector<sl::DeviceProperties>& devs) {
    for (const auto& dev : devs)
        std::cout << "ID : " << dev.id << ", model : " << dev.camera_model << " , S/N : " << dev.serial_number
                  << " , state : " << dev.camera_state << std::endl;
}

struct DeviceLists {
    std::vector<sl::DeviceProperties> stereo;
    std::vector<sl::DeviceProperties> mono;
};

DeviceLists getDeviceListsWithRetry(const int timeout_sec, const int min_count) {
    DeviceLists lists;
    for (int attempt = 0; attempt < timeout_sec && !exit_app; ++attempt) {
        lists.stereo = sl::Camera::getDeviceList();
        lists.mono = sl::CameraOne::getDeviceList();
        const int total = static_cast<int>(lists.stereo.size() + lists.mono.size());
        if (total >= min_count) {
            if (attempt > 0) {
                std::cout << "Camera enumeration reached minimum count after " << (attempt + 1) << " seconds" << std::endl;
            }
            return lists;
        }
        if (attempt + 1 < timeout_sec && !exit_app) {
            sl::sleep_ms(1000);
        }
    }
    return lists;
}

int main(int argc, char** argv) {
    AppOptions options;
    switch (parseArgs(argc, argv, options)) {
    case ParseResult::Help:
        return EXIT_SUCCESS;
    case ParseResult::Error:
        return EXIT_FAILURE;
    default:
        break;
    }

    if (options.record_sensors) {
        std::cout << "Recording mode: image + native sensor metadata" << std::endl;
    } else {
        std::cout << "Recording mode: image only (sensors not required)" << std::endl;
    }
    std::cout << "Camera open timeout: " << options.open_timeout_sec << " seconds" << std::endl;
    std::cout << "Camera enumeration timeout: " << options.enumeration_timeout_sec << " seconds" << std::endl;
    std::cout << "Camera enumeration minimum count: " << options.enumeration_min_cam << std::endl;

    SetCtrlHandler();

    const DeviceLists device_lists = getDeviceListsWithRetry(options.enumeration_timeout_sec, options.enumeration_min_cam);
    const std::vector<sl::DeviceProperties>& dev_stereo_list = device_lists.stereo;
    const std::vector<sl::DeviceProperties>& dev_one_list = device_lists.mono;
    printDeviceInfo(dev_stereo_list);
    printDeviceInfo(dev_one_list);

    const int nb_one = dev_one_list.size();
    const int nb_stereo = dev_stereo_list.size();
    const int nb_total = nb_one + nb_stereo;
    if (nb_total < options.enumeration_min_cam) {
        std::cout << "At least " << options.enumeration_min_cam << " cameras required, found " << nb_total
                  << " after enumeration timeout of " << options.enumeration_timeout_sec << " seconds" << std::endl;
        return EXIT_FAILURE;
    }
    if (nb_total == 0) {
        std::cout << "No ZED Detected, exit program" << std::endl;
        return EXIT_FAILURE;
    }

    std::vector<sl::Camera> zeds_stereo(nb_stereo);
    for (int z = 0; z < nb_stereo; ++z) {
        if (exit_app || !openStereoCamera(zeds_stereo[z], dev_stereo_list[z].serial_number, 30, options.record_sensors, options.open_timeout_sec)) {
            std::cout << "One or more cameras were not detected after timeout of " << options.open_timeout_sec << " seconds" << std::endl;
            closeOpenedCameras(zeds_stereo);
            return EXIT_FAILURE;
        }
    }

    std::vector<sl::CameraOne> zeds_one(nb_one);
    for (int z = 0; z < nb_one; ++z) {
        if (exit_app || !openOneCamera(zeds_one[z], dev_one_list[z].serial_number, 30, options.open_timeout_sec)) {
            std::cout << "One or more cameras were not detected after timeout of " << options.open_timeout_sec << " seconds" << std::endl;
            closeOpenedCameras(zeds_stereo);
            closeOpenedCameras(zeds_one);
            return EXIT_FAILURE;
        }
    }

    for (int z = 0; z < nb_stereo; ++z) {
        if (exit_app || !startRecording(zeds_stereo[z], dev_stereo_list[z].serial_number)) {
            std::cout << "Recording startup aborted before acquisition threads were started" << std::endl;
            stopRecordingAndClose(zeds_stereo);
            stopRecordingAndClose(zeds_one);
            return EXIT_FAILURE;
        }
    }

    for (int z = 0; z < nb_one; ++z) {
        if (exit_app || !startRecording(zeds_one[z], dev_one_list[z].serial_number)) {
            std::cout << "Recording startup aborted before acquisition threads were started" << std::endl;
            stopRecordingAndClose(zeds_stereo);
            stopRecordingAndClose(zeds_one);
            return EXIT_FAILURE;
        }
    }

    std::vector<std::thread> thread_pool(nb_stereo + nb_one);
    for (int z = 0; z < nb_stereo; z++) {
        if (zeds_stereo[z].isOpened())
            thread_pool[z] = std::thread(acquisition<sl::Camera>, std::ref(zeds_stereo[z]));
    }
    for (int z = 0; z < nb_one; z++) {
        if (zeds_one[z].isOpened())
            thread_pool[nb_stereo + z] = std::thread(acquisition<sl::CameraOne>, std::ref(zeds_one[z]));
    }

    std::cout << "Press Ctrl+C to exit" << std::endl;
    while (!exit_app) {
        sl::sleep_ms(20);
    }

    std::cout << "Exit signal, closing ZEDs" << std::endl;
    sl::sleep_ms(100);

    for (auto& th : thread_pool)
        if (th.joinable())
            th.join();

    std::cout << "Program exited" << std::endl;
    return EXIT_SUCCESS;
}
