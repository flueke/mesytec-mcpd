#include "mdll_functions.h"

namespace mesytec
{
namespace mcpd
{

std::error_code mdll_set_thresholds(
    int sock,
    u8 thresholdX,
    u8 thresholdY,
    u8 thresholdAnode)
{
    auto request = make_command_packet(
        CommandType::MdllSetTresholds,
        {
            static_cast<u16>(thresholdX),
            static_cast<u16>(thresholdY),
            static_cast<u16>(thresholdAnode),
        });

    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mdll_set_spectrum(
    int sock,
    u8 shiftX,
    u8 shiftY,
    u8 scaleX,
    u8 scaleY)
{
    auto request = make_command_packet(
        CommandType::MdllSetSpectrum,
        {
            static_cast<u16>(shiftX),
            static_cast<u16>(shiftY),
            static_cast<u16>(scaleX),
            static_cast<u16>(scaleY),
        });

    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mdll_set_pulser(
    int sock,
    bool enable,
    u16 amplitude,
    const MdllChannelPosition &pos)
{
    auto request = make_command_packet(
        CommandType::MdllSetPulser,
        {
            static_cast<u16>(enable),
            static_cast<u16>(amplitude),
            static_cast<u16>(pos),
        });

    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mdll_set_tx_data_set(
    int sock,
    const MdllTxDataSet &ds)
{
    auto request = make_command_packet(
        CommandType::MdllSetTxDataSet,
        std::vector<u16>
        {
            static_cast<u16>(ds),
        });

    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mdll_set_timing_window(
    int sock,
    unsigned tSumLimitXLow,
    unsigned tSumLimitXHigh,
    unsigned tSumLimitYLow,
    unsigned tSumLimitYHigh)
{
    auto request = make_command_packet(
        CommandType::MdllSetTimingWindow,
        {
            static_cast<u16>(0), // not used
            static_cast<u16>(0), // not used
            static_cast<u16>(tSumLimitXLow),
            static_cast<u16>(tSumLimitXHigh),
            static_cast<u16>(tSumLimitYLow),
            static_cast<u16>(tSumLimitYHigh),
        });

    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mdll_set_energy_window(
    int sock,
    u8 lowerThreshold,
    u8 upperThreshold)
{
    auto request = make_command_packet(
        CommandType::MdllSetEnergyWindow,
        {
            static_cast<u16>(lowerThreshold),
            static_cast<u16>(upperThreshold),
            static_cast<u16>(0), // not used
            static_cast<u16>(0), // not used
        });

    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

}
}
