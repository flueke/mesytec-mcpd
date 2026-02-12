#ifndef B2F39267_6303_47A9_B89F_9628979A4CCB
#define B2F39267_6303_47A9_B89F_9628979A4CCB

#include <spdlog/spdlog.h>
#include <optional>

namespace mesytec::mcpd
{

std::shared_ptr<spdlog::logger>
create_logger(const std::string &name, const std::vector<spdlog::sink_ptr> &sinks = {});

std::shared_ptr<spdlog::logger> default_logger();

void set_default_logger(std::shared_ptr<spdlog::logger> logger);

void set_global_log_level(spdlog::level::level_enum level);

std::optional<spdlog::level::level_enum> log_level_from_string(const std::string &levelName);

std::string log_level_to_string(const spdlog::level::level_enum &level);

}

#endif /* B2F39267_6303_47A9_B89F_9628979A4CCB */
