#ifndef __MDLL_FUNCTIONS_H__
#define __MDLL_FUNCTIONS_H__

#include "mcpd_functions.h"

namespace mesytec
{
namespace mcpd
{

std::error_code mdll_set_thresholds(
    int sock,
    u8 thresholdX,
    u8 thresholdY,
    u8 thresholdAnode);

std::error_code mdll_set_spectrum(
    int sock,
    u8 shiftX,
    u8 shiftY,
    u8 scaleX,
    u8 scaleY);

std::error_code mdll_set_pulser(
    int sock,
    bool enable,
    u16 amplitude,
    const MdllChannelPosition &pos);

std::error_code mdll_set_tx_data_set(
    int sock,
    const MdllTxDataSet &ds);


std::error_code mdll_set_timing_window(
    int sock,
    unsigned tSumLimitXLow,
    unsigned tSumLimitXHigh,
    unsigned tSumLimitYLow,
    unsigned tSumLimitYHigh);


std::error_code mdll_set_energy_window(
    int sock,
    u8 lowerThreshold,
    u8 upperThreshold);

}
}

#endif /* __MDLL_FUNCTIONS_H__ */
