#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>

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

// Directories to exclude from scanning
const std::set<std::string> EXCLUDED_DIRS = {
    "Windows",
    "Program Files",
    "Program Files (x86)",
    "ProgramData",
    "$Recycle.Bin",
    "System Volume Information",
    "$RECYCLE.BIN",
    "Recovery",
    "boot",
    "efi",
    ".git",
    "node_modules",
    ".cache",
    "Thumbs.db",
    "pagefile.sys",
    "hiberfil.sys",
    "swapfile.sys"
};

// Format list to scan (extension -> format info)
void init_format_list(const Config& cfg) {
    // Image formats
    g_formats["jpg"] = {"jpg", FileType::Image, "JPEG Image"};
    g_formats["jpeg"] = {"jpg", FileType::Image, "JPEG Image"};  // jpeg ext maps to jpg
    g_formats["png"] = {"png", FileType::Image, "PNG Image"};
    g_formats["gif"] = {"gif", FileType::Image, "GIF Image"};
    g_formats["bmp"] = {"bmp", FileType::Image, "BMP Image"};
    g_formats["tiff"] = {"tiff", FileType::Image, "TIFF Image"};
    g_formats["tif"] = {"tiff", FileType::Image, "TIFF Image"};
    g_formats["webp"] = {"webp", FileType::Image, "WebP Image"};
    g_formats["heic"] = {"heic", FileType::Image, "HEIC Image"};
    g_formats["heif"] = {"heic", FileType::Image, "HEIF Image"};
    g_formats["orf"] = {"orf", FileType::Image, "Olympus RAW"};

    // Video formats
    g_formats["mp4"] = {"mp4", FileType::Video, "MP4 Video"};
    g_formats["mov"] = {"mp4", FileType::Video, "QuickTime Movie"};
    g_formats["avi"] = {"avi", FileType::Video, "AVI Video"};
    g_formats["mkv"] = {"mkv", FileType::Video, "Matroska Video"};
    g_formats["webm"] = {"mkv", FileType::Video, "WebM Video"};
    g_formats["flv"] = {"flv", FileType::Video, "Flash Video"};
    g_formats["mts"] = {"mts", FileType::Video, "MPEG Transport Stream"};
    g_formats["m2ts"] = {"mts", FileType::Video, "MPEG Transport Stream"};
    g_formats["wmv"] = {"wmv", FileType::Video, "Windows Media Video"};

    // Audio formats
    g_formats["mp3"] = {"mp3", FileType::Audio, "MP3 Audio"};
    g_formats["flac"] = {"flac", FileType::Audio, "FLAC Audio"};
    g_formats["wav"] = {"wav", FileType::Audio, "WAV Audio"};
    g_formats["m4a"] = {"m4a", FileType::Audio, "M4A Audio"};

    // Document formats
    g_formats["pdf"] = {"pdf", FileType::Document, "PDF Document"};
    g_formats["doc"] = {"doc", FileType::Document, "Word Document"};
    g_formats["xls"] = {"doc", FileType::Document, "Excel Document"};
    g_formats["ppt"] = {"doc", FileType::Document, "PowerPoint Document"};

    // Archive formats
    g_formats["zip"] = {"zip", FileType::Archive, "ZIP Archive"};
    g_formats["7z"] = {"7z", FileType::Archive, "7-Zip Archive"};
    g_formats["rar"] = {"rar", FileType::Archive, "RAR Archive"};

    // If specific formats requested, filter list
    if (!cfg.specific_formats.empty()) {
        std::map<std::string, FormatInfo> filtered;
        for (const auto& [ext, info] : g_formats) {
            if (cfg.specific_formats.count(ext) || cfg.specific_formats.count(info.extension)) {
                filtered[ext] = info;
            }
        }
        g_formats = std::move(filtered);
    }
}

bool should_skip_dir(const fs::path& path) {
    std::string name = path.filename().string();
    // Skip hidden directories (starting with .)
    if (!name.empty() && name[0] == '.') return true;
    // Skip excluded list
    return EXCLUDED_DIRS.count(name) > 0;
}

bool should_skip_entry(const fs::directory_entry& entry) {
    // Skip symlinks
    if (entry.is_symlink()) return true;
    // Skip temporary files
    std::string name = entry.path().filename().string();
    if (name.find('~') != std::string::npos) return true;
    if (name.size() > 4 && name.substr(name.size() - 4) == ".tmp") return true;
    return false;
}

void scan_directory(const fs::path& root, const Config& cfg, size_t& total_scanned) {
    if (!fs::exists(root)) {
        if (cfg.verbose) std::cout << "  Skipping non-existent: " << root << "\n";
        return;
    }

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator();
         ++it) {
        try {
            const auto& entry = *it;

            if (should_skip_entry(entry)) {
                continue;
            }

            if (entry.is_directory()) {
                if (should_skip_dir(entry.path())) {
                    if (cfg.verbose) std::cout << "  Skipping dir: " << entry.path() << "\n";
                    it.disable_recursion_pending();
                }
                continue;
            }

            if (!entry.is_regular_file()) continue;

            total_scanned++;

            // Check extension
            std::string ext = entry.path().extension().string();
            if (ext.empty()) continue;

            // Remove dot and convert to lowercase
            ext = ext.substr(1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            // Find format
            auto fit = g_formats.find(ext);
            if (fit == g_formats.end()) continue;

            // Check file size
            uint64_t file_size = entry.file_size();
            if (file_size > cfg.max_file_size_mb * 1024 * 1024) continue;
            if (file_size == 0) continue;

            // Get target extension (handle aliases like jpeg->jpg)
            std::string target_ext = fit->second.extension;
            auto& files = g_formats[target_ext].found_files;

            // Check if already collected enough samples
            if (files.size() >= cfg.samples_per_format) continue;

            // Avoid duplicate files
            if (std::find(files.begin(), files.end(), entry.path()) != files.end()) continue;

            files.push_back(entry.path());
            if (cfg.verbose) {
                std::cout << "  Found [" << target_ext << "]: " << entry.path()
                          << " (" << file_size / 1024 << " KB)\n";
            }

        } catch (const std::exception& e) {
            if (cfg.verbose) {
                std::cout << "  Error: " << e.what() << "\n";
            }
        }
    }
}

void scan_drives(const Config& cfg) {
    size_t total_scanned = 0;

    for (const auto& drive : cfg.drives) {
        fs::path root(drive + ":\\");
        std::cout << "\nScanning " << root << "...\n";
        scan_directory(root, cfg, total_scanned);
    }

    std::cout << "\nTotal files scanned: " << total_scanned << "\n";
}

void copy_files(const Config& cfg) {
    if (cfg.dry_run) {
        std::cout << "\n[DRY RUN] Would copy:\n";
    } else {
        // Create output directory
        std::error_code ec;
        fs::create_directories(cfg.output_dir, ec);

        if (ec) {
            std::cerr << "Error creating output directory: " << ec.message() << "\n";
            return;
        }
    }

    size_t total_copied = 0;
    size_t total_skipped = 0;

    for (const auto& [ext, info] : g_formats) {
        // Only process target extensions (skip aliases like jpeg->jpg)
        if (ext != info.extension) continue;

        if (info.found_files.empty()) {
            std::cout << "  [" << ext << "] No files found\n";
            continue;
        }

        // Create format subdirectory
        fs::path format_dir = cfg.output_dir / ext;
        if (!cfg.dry_run) {
            fs::create_directories(format_dir);
        }

        std::cout << "  [" << ext << "] Copying " << info.found_files.size() << " files...\n";

        for (const auto& src : info.found_files) {
            fs::path dst = format_dir / src.filename();

            // Handle filename conflicts
            if (fs::exists(dst)) {
                // Add sequence number
                int counter = 1;
                std::string stem = src.stem().string();
                std::string extension = src.extension().string();
                while (fs::exists(dst)) {
                    dst = format_dir / (stem + "_" + std::to_string(counter) + extension);
                    counter++;
                }
            }

            if (cfg.dry_run) {
                std::cout << "    " << src << " -> " << dst << "\n";
            } else {
                std::error_code ec;
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    std::cout << "    Error copying " << src << ": " << ec.message() << "\n";
                    total_skipped++;
                } else {
                    if (cfg.verbose) {
                        std::cout << "    Copied: " << src.filename() << "\n";
                    }
                    total_copied++;
                }
            }
        }
    }

    std::cout << "\nCopied: " << total_copied << " files\n";
    if (total_skipped > 0) {
        std::cout << "Skipped (errors): " << total_skipped << " files\n";
    }
}

void generate_report(const Config& cfg) {
    fs::path report_path = cfg.output_dir / "collection_report.txt";

    std::ofstream report(report_path);
    if (!report) {
        std::cerr << "Error: Could not create report file\n";
        return;
    }

    report << "Sample Collection Report\n";
    report << "========================\n\n";

    report << "Configuration:\n";
    report << "  Drives: ";
    for (const auto& d : cfg.drives) report << d << ": ";
    report << "\n";
    report << "  Max file size: " << cfg.max_file_size_mb << " MB\n";
    report << "  Samples per format: " << cfg.samples_per_format << "\n\n";

    report << "Results:\n";
    report << "--------\n\n";

    size_t formats_found = 0;
    size_t formats_missing = 0;

    for (const auto& [ext, info] : g_formats) {
        if (ext != info.extension) continue;  // Skip aliases

        report << "[" << ext << "] " << info.description << "\n";

        if (info.found_files.empty()) {
            report << "  Status: NO SAMPLES FOUND\n";
            formats_missing++;
        } else {
            report << "  Status: Found " << info.found_files.size() << " files\n";
            for (const auto& f : info.found_files) {
                report << "  - " << f.filename() << "\n";
            }
            formats_found++;
        }
        report << "\n";
    }

    report << "Summary:\n";
    report << "  Formats with samples: " << formats_found << "\n";
    report << "  Formats missing: " << formats_missing << "\n";

    report.close();
    std::cout << "\nReport saved to: " << report_path << "\n";
}

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

    // Initialize format list
    init_format_list(cfg);
    std::cout << "Tracking " << g_formats.size() << " format extensions\n";

    // Scan drives
    scan_drives(cfg);

    // Copy files
    copy_files(cfg);

    // Generate report
    generate_report(cfg);

    std::cout << "\nDone.\n";
    return 0;
}