#ifndef B2F39267_6303_47A9_B89F_9628979A4CCB
#define B2F39267_6303_47A9_B89F_9628979A4CCB

#include <spdlog/spdlog.h>
#include <optional>
#include "mesytec-mcpd_export.h"

namespace mesytec::mcpd
{

std::shared_ptr<spdlog::logger>
MESYTEC_MCPD_EXPORT create_logger(const std::string &name, const std::vector<spdlog::sink_ptr> &sinks = {});

std::shared_ptr<spdlog::logger> MESYTEC_MCPD_EXPORT default_logger();

void MESYTEC_MCPD_EXPORT set_default_logger(std::shared_ptr<spdlog::logger> logger);

void MESYTEC_MCPD_EXPORT set_global_log_level(spdlog::level::level_enum level);

std::optional<spdlog::level::level_enum> MESYTEC_MCPD_EXPORT log_level_from_string(const std::string &levelName);

std::string MESYTEC_MCPD_EXPORT log_level_to_string(const spdlog::level::level_enum &level);

}

#endif /* B2F39267_6303_47A9_B89F_9628979A4CCB */
