#pragma once

#include <optional>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace aura::cli {
inline constexpr const char* version = "0.17.3";
class TerminalSession {
public:
    TerminalSession();
    ~TerminalSession();
    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;
};
class FilePreview {
    bool active_ = false;
public:
    explicit FilePreview(const std::string& path);
    ~FilePreview();
    FilePreview(const FilePreview&) = delete;
    FilePreview& operator=(const FilePreview&) = delete;
};
std::vector<std::string> file_references(const std::string& input);
void clear_transcript();
void print_header();
void print_error(const std::string& message);
void print_response_label();
void print_history();
void show_command_center();
void print_queue();
void unqueue();
int run_shell(const std::string& command);
int run_balance_training(const std::string& command);
void dictate_prompt();
std::optional<std::string> read_prompt();
struct JointControl {
    std::string name;
    double value, minimum, maximum;
    bool clamped = false;
};
using JointControls = std::array<JointControl, 4>;
struct ControlObservation {
    double pelvis_height = 0.0;
    double pelvis_uprightness = 0.0;
    double age_seconds = 0.0;
    bool is_standing = false;
    std::array<double, 4> joint_angles{};
    double simulation_time = 0.0, steps = 0.0, speed = 0.0, angular_speed = 0.0;
    bool paused = false, has_motion = false;
    // 16 records: world position xyz, orientation wxyz, dimensions xyz.
    std::vector<std::array<double, 10>> body_pose;

};
// A separate screen that restores the CLI transcript on return. Empty save
// results mean success; errors leave the last successfully applied values intact.
void edit_joint_controls(JointControls controls,
                         const std::function<std::string(const JointControls&)>& save,
                         const std::function<std::optional<ControlObservation>()>& observe);
}
