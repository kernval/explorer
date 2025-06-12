#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <atomic>
#include <chrono>
#include <unordered_set>
#include <windows.h>

namespace fs = std::filesystem;

#define RED "\033[31m"
#define RESET "\033[0m"
#define TEXT "\033[38;2;183;189;248m"

void EnableVirtualTerminal() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}

class FileExplorer {
    fs::path current_path;
    std::atomic<bool> listening{ false };
    std::thread listener_thread;

    void list_files() const {
        std::cout << TEXT <<"Current: " << current_path << RESET << std::endl;
        try {
            for (const auto& entry : fs::directory_iterator(current_path, fs::directory_options::skip_permission_denied)) {
                std::cout << TEXT << (entry.is_directory() ? "[D] " : "[F] ") << entry.path().filename().string() << RESET << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cout << TEXT << "Error: " << e.what() << RESET << std::endl;
        }
    }

    bool force_delete(const fs::path& file) {
#ifdef _WIN32
        std::string cmd = "attrib -H -S \"" + file.string() + "\"";
        system(cmd.c_str());
#endif
        std::error_code ec;
        bool result = fs::remove(file, ec);
        if (!result || ec) {
            std::cout << TEXT << "Failed to delete: " << file << " (" << ec.message() << ")" << RESET << std::endl;
        }
        return result && !ec;
    }

    std::unordered_set<std::string> snapshot_files() const {
        std::unordered_set<std::string> files;
        for (const auto& entry : fs::directory_iterator(current_path, fs::directory_options::skip_permission_denied)) {
            files.insert(entry.path().filename().string());
        }
        return files;
    }

    void listen_for_changes(const std::string& flag) {
        listening = true;
        std::cout << TEXT << "Listening for file changes in: " << current_path << RESET << std::endl;
        bool log_mode = (flag == "-log");
        fs::path log_dir = "C:/KExplorer";
        if (log_mode) {
            std::error_code ec;
            if (!fs::exists(log_dir)) {
                fs::create_directories(log_dir, ec);
                if (ec) {
                    std::cout << TEXT << "Failed to create log directory: " << log_dir << " (" << ec.message() << ")" << RESET << std::endl;
                    return;
                }
            }
        }
        auto prev_files = snapshot_files();
        while (listening) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            auto curr_files = snapshot_files();

            for (const auto& f : curr_files) {
                if (prev_files.find(f) == prev_files.end()) {
                    std::cout << TEXT << "[Created] " << f << RESET << std::endl;
                    if (log_mode) {
                        fs::path src = current_path / f;
                        fs::path dst = log_dir / f;
                        std::error_code ec;
                        if (fs::is_regular_file(src)) {
                            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
                            if (ec) {
                                std::cout << TEXT << "Failed to log file: " << src << " (" << ec.message() << ")" << RESET << std::endl;
                            }
                        }
                    }
                }
            }

            for (const auto& f : prev_files) {
                if (curr_files.find(f) == curr_files.end()) {
                    std::cout << TEXT << "[Deleted] " << f << RESET << std::endl;
                }
            }
            prev_files = std::move(curr_files);
        }
        std::cout << TEXT << "Stopped listening." << RESET << std::endl;
    }

    bool copy_file_visible(const std::string& filename, const std::string& dest_dir = "C:/KExplorer") {
        fs::path src = current_path / filename;
        fs::path dst_dir = dest_dir.empty() ? "C:/KExplorer" : dest_dir;
        fs::path dst = dst_dir / filename;

        if (!fs::exists(src) || !fs::is_regular_file(src)) {
            std::cout << TEXT << "Source file not found: " << src << RESET << std::endl;
            return false;
        }

#ifdef _WIN32
        std::string cmd = "attrib -H -S \"" + src.string() + "\"";
        system(cmd.c_str());
#endif

        std::error_code ec;
        if (!fs::exists(dst_dir)) {
            fs::create_directories(dst_dir, ec);
            if (ec) {
                std::cout << TEXT << "Failed to create directory: " << dst_dir << " (" << ec.message() << ")" << RESET << std::endl;
                return false;
            }
        }
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cout << TEXT << "Failed to copy file: " << ec.message() << RESET << std::endl;
            return false;
        }
#ifdef _WIN32
        std::string cmd2 = "attrib -H -S \"" + dst.string() + "\"";
        system(cmd2.c_str());
#endif
        std::cout << TEXT << "Copied " << filename << " to " << dst_dir << RESET << std::endl;
        return true;
    }

    void stop_listening() {
        if (listening) {
            listening = false;
            if (listener_thread.joinable()) {
                listener_thread.join();
            }
        }
    }

public:
    FileExplorer(const fs::path& start) : current_path(start) {}

    ~FileExplorer() {
        stop_listening();
    }

    void run() {
        std::string input;
        list_files();
        while (true) {
            std::cout << RED << "\nroot> " << RESET;
            if (!std::getline(std::cin, input)) break;
            if (input == "exit") {
                stop_listening();
                break;
            }
            else if (input.rfind("jump ", 0) == 0) {
                stop_listening();
                std::string dir = input.substr(5);
                fs::path new_path = dir;
                if (!new_path.is_absolute())
                    new_path = current_path / new_path;
                if (fs::exists(new_path) && fs::is_directory(new_path)) {
                    system("cls");
                    current_path = fs::canonical(new_path);
                    list_files();
                }
                else {
                    std::cout << TEXT << "Directory not found: " << new_path << RESET << std::endl;
                }
            }
            else if (input.rfind("rem ", 0) == 0) {
                std::string file = input.substr(4);
                fs::path file_path = current_path / file;
                if (fs::exists(file_path)) {
                    force_delete(file_path);
                }
                else {
                    std::cout << TEXT << "File not found: " << file_path << RESET << std::endl;
                }
            }
            else if (input == "list") {
                system("cls");
                list_files();
            }
            else if (input.rfind("listen", 0) == 0) {
                std::string flag = "-normal";
                if (input.length() > 6) {
                    std::string arg = input.substr(6);
                    arg.erase(0, arg.find_first_not_of(" \t"));
                    if (arg == "-log")
                        flag = "-log";
                }
                if (!listening) {
                    listener_thread = std::thread([this, flag] { listen_for_changes(flag); });
                    while (listening) {
                        std::string listen_input;
                        std::cout << TEXT << "root (listening, type 'stop' to end) > " << RESET;
                        if (!std::getline(std::cin, listen_input)) break;
                        if (listen_input == "stop") {
                            stop_listening();
                            break;
                        }
                    }
                }
                else {
                    std::cout << TEXT << "Already listening for changes." << RESET << std::endl;
                }
            }
            else if (input == "stop") {
                stop_listening();
            }
            else if (input == "cls") {
                system("cls");

            }

            else if (input.rfind("copy ", 0) == 0) {
                std::string args = input.substr(5);
                size_t space = args.find(' ');
                std::string file, dir;
                if (space == std::string::npos) {
                    file = args;
                    dir = "C:/KExplorer";
                }
                else {
                    file = args.substr(0, space);
                    dir = args.substr(space + 1);
                    dir.erase(0, dir.find_first_not_of(" \t"));
                    if (dir.empty()) dir = "C:/KExplorer";
                }
                copy_file_visible(file, dir);
            }

            else if (input == "help") {
                std::cout << TEXT << "Usage: jump <Directory> / rem <Filename> / list / listen [-normal|-log] / copy <Filename> [Directory] / help / cls / exit" << RESET << std::endl;
            }

            else {
                std::cout << TEXT << "Unknown command. Type 'help' for all commands" << RESET << std::endl;
            }
        }
    }
};

void Launch() {
    FileExplorer explorer(fs::current_path());
    explorer.run();
}

int main()
{
    EnableVirtualTerminal();
    SetConsoleTitleA("Hidden Explorer / Log System / Security Flag Bypass");
    Launch();
}
