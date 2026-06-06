#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "format_registry.hpp"

using namespace disk_recover;
namespace fs = std::filesystem;

// Configuration
struct Config {
    std::vector<std::string> drives = {"C", "D"};
    fs::path output_dir = "tests/data/real_samples";
    uint64_t max_file_size_mb = 100;
    size_t samples_per_format = 5;
    bool dry_run = false;
    bool verbose = false;
    std::set<std::string> specific_formats;
};

// Format extension mapping (from FormatRegistry)
struct FormatInfo {
    std::string extension;      // lowercase extension
    FileType file_type;
    std::string description;
    std::vector<fs::path> found_files;
};

// Global state
std::map<std::string, FormatInfo> g_formats;

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
              << "  --drives <C,D>      Drives to scan (default: C,D)\n"
              << "  --output <dir>      Output directory (default: tests/data/real_samples)\n"
              << "  --max-size <MB>     Max file size in MB (default: 100)\n"
              << "  --samples <N>       Samples per format (default: 5)\n"
              << "  --formats <list>    Specific formats (default: all)\n"
              << "  --dry-run           List files only, don't copy\n"
              << "  --verbose           Verbose output\n"
              << "  --help              Show this help\n";
}

bool parse_args(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--drives" && i + 1 < argc) {
            std::string drives = argv[++i];
            cfg.drives.clear();
            size_t pos = 0;
            while ((pos = drives.find(',')) != std::string::npos) {
                cfg.drives.push_back(drives.substr(0, pos));
                drives.erase(0, pos + 1);
            }
            if (!drives.empty()) cfg.drives.push_back(drives);
        } else if (arg == "--output" && i + 1 < argc) {
            cfg.output_dir = argv[++i];
        } else if (arg == "--max-size" && i + 1 < argc) {
            cfg.max_file_size_mb = std::stoull(argv[++i]);
        } else if (arg == "--samples" && i + 1 < argc) {
            cfg.samples_per_format = std::stoul(argv[++i]);
        } else if (arg == "--formats" && i + 1 < argc) {
            std::string formats = argv[++i];
            cfg.specific_formats.clear();
            size_t pos = 0;
            while ((pos = formats.find(',')) != std::string::npos) {
                cfg.specific_formats.insert(formats.substr(0, pos));
                formats.erase(0, pos + 1);
            }
            if (!formats.empty()) cfg.specific_formats.insert(formats);
        } else if (arg == "--dry-run") {
            cfg.dry_run = true;
        } else if (arg == "--verbose") {
            cfg.verbose = true;
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        return 1;
    }

    std::cout << "Sample Collector for Disk Recover\n";
    std::cout << "==================================\n";
    std::cout << "Drives: ";
    for (const auto& d : cfg.drives) std::cout << d << ": ";
    std::cout << "\n";
    std::cout << "Output: " << cfg.output_dir << "\n";
    std::cout << "Max size: " << cfg.max_file_size_mb << " MB\n";
    std::cout << "Samples per format: " << cfg.samples_per_format << "\n";

    // TODO: Initialize format list
    // TODO: Scan drives
    // TODO: Copy files
    // TODO: Generate report

    std::cout << "\nDone.\n";
    return 0;
}