#pragma once

#include <optional>
#include "../include/aura/sim/ActionSpec.hpp"
#include <array>
#include <functional>
#include <string>
#include <vector>
#include "app/CLIUpdate.hpp"

namespace aura::cli {
    inline constexpr const char *release_version = "0.28.0";
    inline constexpr const char *version = "0.28.0+" AURA_CLI_BUILD_ID;

    class TerminalSession {
        bool retain_screen_ = false;
    public:
        TerminalSession();

        ~TerminalSession();

        void retain_screen_for_reload();

        TerminalSession(const TerminalSession &) = delete;

        TerminalSession &operator=(const TerminalSession &) = delete;
    };

    class FilePreview {
        bool active_ = false;

    public:
        explicit FilePreview(const std::string &path);

        ~FilePreview();

        FilePreview(const FilePreview &) = delete;

        FilePreview &operator=(const FilePreview &) = delete;
    };

    std::vector<std::string> file_references(const std::string &input);

    void clear_transcript();

    void print_header();

    void print_error(const std::string &message);

    void print_response_label();

    void print_history();

    bool clear_history();

    void show_command_center();

    bool returning_home();

    bool pending_reload_input();

    void set_command_center_handler(std::function < int(const std::string &, const std::string &) > handler);

    void print_queue();

    void unqueue();

    int run_experiment_library(const std::string &command);
    int run_shell(const std::string &command, const std::string &title = "", const std::string &model_kind = "");

    int run_nn_training(const std::string &command);

    int run_model_tools(const std::string &command);

    int run_evaluation(const std::string &command);

    int run_critic_training(const std::string &command);

    std::optional<std::string> select_model(const std::string &directory, const std::string &action = "watch");

    struct TrainingSelection {
        std::string mode, name, checkpoint;
        std::string device = "cpu";
        int episodes = 32, batches = 3, agents = 0;
    };

    std::optional<TrainingSelection> select_training(const std::string &directory);
    std::optional<TrainingSelection> configure_training_run(TrainingSelection selection);

    // Archived feedback-search dashboard, no longer registered as a CLI command.
    int run_balance_training(const std::string &command);

    void dictate_prompt();

    std::optional<std::string> read_prompt();

    struct JointControl {
        std::string name;
        double value, minimum, maximum;
        bool clamped = false;
    };

    using JointControls = std::array<JointControl, sim::actionSpecs.size()>;
    inline constexpr double control_freshness_seconds = 0.5;

    struct ControlObservation {
        double pelvis_height = 0.0;
        double pelvis_uprightness = 0.0;
        double age_seconds = 0.0;
        bool is_standing = false; // historical telemetry only
        std::string balance_state = "Unknown";
        std::array<double, sim::actionSpecs.size()> joint_angles{};
        std::array<bool, sim::actionSpecs.size()> motor_enabled{};
        bool has_motor_states = false;
        double simulation_time = 0.0, steps = 0.0, speed = 0.0, angular_speed = 0.0;
        bool paused = false, has_motion = false;
        // One record per part: world position xyz, orientation wxyz, dimensions xyz.
        std::vector<std::array<double, 10> > body_pose;
    };

    // A separate screen that restores the CLI transcript on return. Empty save
    // results mean success; errors leave the last successfully applied values intact.
    void edit_joint_controls(JointControls controls,
                             const std::function<std::string(const JointControls &)> &save,
                             const std::function<std::optional<ControlObservation>()> &observe);
}
