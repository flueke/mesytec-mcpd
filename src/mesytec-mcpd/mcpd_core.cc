#include "mcpd_core.h"

namespace
{
    class McpdErrorCategory: public std::error_category
    {
        const char *name() const noexcept override
        {
            return "mcpd_error";
        }

        std::string message(int ev) const override
        {
            using mesytec::mcpd::CommandError;

            switch (static_cast<CommandError>(ev))
            {
                case CommandError::NoError:
                    return "No Error";

                case CommandError::IdMismatch:
                    return "ID mismatch";

                default:
                    break;
            }

            return fmt::format("Unknown error code {}", ev);
        }
    };

    const McpdErrorCategory theMcpdErrorCategory {};
}

namespace mesytec
{
namespace mcpd
{

std::error_code make_error_code(CommandError error)
{
    return { static_cast<int>(error), theMcpdErrorCategory };
}

const char *to_string(const CommandType &cmd)
{
    switch (cmd)
    {
        case CommandType::Reset: return "Reset";
        case CommandType::StartDAQ: return "StartDAQ";
        case CommandType::StopDAQ: return "StopDAQ";
        case CommandType::ContinueDAQ: return "ContinueDAQ";
        case CommandType::SetId: return "SetId";
        case CommandType::SetProtoParams: return "SetProtoParams";
        case CommandType::SetTiming: return "SetTiming";
        case CommandType::SetClock: return "SetClock";
        case CommandType::SetRunId: return "SetRunId";
        case CommandType::SetCell: return "SetCell";
        case CommandType::SetAuxTimer: return "SetAuxTimer";
        case CommandType::SetParam: return "SetParam";
        case CommandType::GetParams: return "GetParams";
        case CommandType::SetGain: return "SetGain";
        case CommandType::SetThreshold: return "SetThreshold";
        case CommandType::SetPulser: return "SetPulser";
        case CommandType::SetMpsdMode: return "SetMpsdMode";
        case CommandType::SetDAC: return "SetDAC";
        case CommandType::SendSerial: return "SendSerial";
        case CommandType::ReadSerial: return "ReadSerial";
        case CommandType::SetTTLOutputs: return "SetTTLOutputs";
        case CommandType::GetBusCapabilities: return "GetBusCapabilities";
        case CommandType::SetBusCapabilities: return "SetBusCapabilities";
        case CommandType::GetMpsdParams: return "GetMpsdParams";
        case CommandType::SetFastTxMode: return "SetFastTxMode";
        case CommandType::SetMstdGain: return "SetMstdGain";
        case CommandType::ReadIds: return "ReadIds";
        case CommandType::GetVersion: return "GetVersion";

        case CommandType::ReadPeripheralRegister: return "ReadPeripheralRegister";
        case CommandType::WritePeripheralRegister: return "WritePeripheralRegister";

        case CommandType::MdllSetTresholds: return "MdllSetTresholds";
        case CommandType::MdllSetSpectrum: return "MdllSetSpectrum";
        case CommandType::MdllSetPulser: return "MdllSetPulser";
        case CommandType::MdllSetTxDataSet: return "MdllSetTxDataSet";
        case CommandType::MdllSetTimingWindow: return "MdllSetTimingWindow";
        case CommandType::MdllSetEnergyWindow: return "MdllSetEnergyWindow";

        case CommandType::WriteRegister: return "WriteRegister";
        case CommandType::ReadRegister: return "ReadRegister";
    }

    return "<unknown CommandType>";
}

}
}
