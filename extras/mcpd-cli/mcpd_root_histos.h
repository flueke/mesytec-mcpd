#ifndef __MESYTEC_MCPD_ROOT_HISTOS_H__
#define __MESYTEC_MCPD_ROOT_HISTOS_H__

#include <TFile.h>
#include <TH1D.h>
#include <TH2D.h>
#include <mesytec-mcpd/mesytec-mcpd.h>

namespace mesytec::mcpd
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

    // These use a linear index consisting of mcpd, mpsd and channel ids. See
    // get_histo(), etc below.
    std::vector<TH1D *> amplitudes;
    std::vector<TH1D *> positions;
    std::vector<TH1D *> timestamps;

    // Hacky place to store MDLL histos, graphs and values. This should be redesigned at some point.
    struct MdllHistos
    {
        TH1D *amplitudes = nullptr;
        TH1D *xPositions = nullptr;
        TH1D *yPositions = nullptr;
        TH2D *xyPositions = nullptr;

        TH1D *packetTimestamps = nullptr;
        TH1D *eventTimestamps = nullptr;
        TH1D *fullTimestamps = nullptr;

        TH1D *packetTimestampDeltas = nullptr;
        TH1D *eventTimestampDeltas = nullptr;
        TH1D *fullTimestampDeltas = nullptr;

        std::optional<uint64_t> lastPacketTimestamp;
        std::optional<uint64_t> lastEventTimestamp;
        std::optional<uint64_t> lastFullTimestamp;

        // Used to create value-over-time graphs at the end of a run.
        // This can grow indefinitely. Makes the OOM-killer happy.
        struct GraphStorage
        {
            std::vector<double> packet_timestamps; // packet stamp, aka buffer stamp
            std::vector<double> event_timestamps;  // the relative timestamp transmitted with each event
            std::vector<double> full_timestamps;   // calculated full event stamp: packet stamp + event stamp
            std::vector<double> amplitudes;
            std::vector<double> xPositions;
            std::vector<double> yPositions;
        };

        GraphStorage graphStorage;
    };

    // For MDLL data. Indexed by MDLL device id.
    std::vector<std::unique_ptr<MdllHistos>> mdllHistos;

    bool enableMdllGraphs = false;

    RootHistoContext(RootHistoContext &&) = default;
    RootHistoContext &operator=(RootHistoContext &&) = default;
    ~RootHistoContext();
};

RootHistoContext create_histo_context(const std::string &outputFilename);
void root_histos_process_packet(RootHistoContext &rootContext, const DataPacket &packet);
void root_histos_finalize(RootHistoContext &rootContext);

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

#endif /* __MCPD_ROOT_HISTOS_H__ */
