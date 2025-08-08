#ifndef __MESYTEC_MCPD_ROOT_HISTOS_H__
#define __MESYTEC_MCPD_ROOT_HISTOS_H__

#include <TFile.h>
#include <TH1D.h>
#include <TH2D.h>
#include <mesytec-mcpd/mesytec-mcpd.h>

namespace mesytec
{
namespace mcpd
{

struct RootHistoContext
{
    std::unique_ptr<TFile> histoOutFile;

    // neutron events [mcpdId][mpsdId][channel]["amplitude"]
    // bits             8       3       5        10
    //
    // neutron event: [mcpdId][mpsdId][channel]["position"]
    // bits             8       3       5        10
    //
    // Maybe:
    // any event:     [mcpdId][mpsdId][channel]["timestamp"]
    // bits             8       3       5        19

    std::vector<TH1D *> amplitudes;
    std::vector<TH1D *> positions;
    std::vector<TH1D *> timestamps;

    TH1D *mdll_amplitudes;
    TH1D *mdll_xPositions;
    TH1D *mdll_yPositions;
    TH2D *mdll_xyPositions;

    RootHistoContext(RootHistoContext &&) = default;
    RootHistoContext &operator=(RootHistoContext &&) = default;
    ~RootHistoContext();
};

RootHistoContext create_histo_context(const std::string &outputFilename);
void root_histos_process_packet(RootHistoContext &rootContext, const DataPacket &packet);

inline size_t linear_address(unsigned mcpdId, unsigned mpsdId, unsigned channel)
{
    return (  (channel & 0b11111u)
            | (mpsdId & 0b111u) << 5
            | (mcpdId & 0xffu) << 8
           );
}

inline TH1D *get_histo(const std::vector<TH1D *> histos, unsigned mcpdId, unsigned mpsdId, unsigned channel)
{
    auto idx = linear_address(mcpdId, mpsdId, channel);
    return (idx < histos.size()) ? histos[idx] : nullptr;
}

inline TH1D *get_amplitude_histo(RootHistoContext &ctx, unsigned mcpdId, unsigned mpsdId, unsigned channel)
{
    return get_histo(ctx.amplitudes, mcpdId, mpsdId, channel);
}

inline TH1D *get_position_histo(RootHistoContext &ctx, unsigned mcpdId, unsigned mpsdId, unsigned channel)
{
    return get_histo(ctx.positions, mcpdId, mpsdId, channel);
}

inline TH1D *get_timestamp_histo(RootHistoContext &ctx, unsigned mcpdId, unsigned mpsdId, unsigned channel)
{
    return get_histo(ctx.timestamps, mcpdId, mpsdId, channel);
}

}
}

#endif /* __MCPD_ROOT_HISTOS_H__ */
