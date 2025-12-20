#pragma once

#include <string>

struct Args {
    // Directory containing config files (e.g. static_info.json).
    std::string config_path;

    // Task upload root directory (e.g. workspace/master/data/upload).
    // Gateway resolves file path as: <task_path>/<client_ip>/<filename>.
    std::string task_path;

    // Keep uploaded files after successful completion (default: delete).
    bool keep_upload = false;
};
