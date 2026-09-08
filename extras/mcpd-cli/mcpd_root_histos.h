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
    struct McpdHistos
    {
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
    };

    struct MdllHistos
    {
        TH1D *amplitudes = nullptr;
        TH1D *xPositions = nullptr;
        TH1D *yPositions = nullptr;
        TH2D *xyPositions = nullptr;

        // Used to create value-over-time graphs at the end of a run.
        // This can grow indefinitely. Makes the OOM-killer happy.
        struct GraphStorage
        {
            std::vector<double> amplitudes;
            std::vector<double> xPositions;
            std::vector<double> yPositions;
        };

        GraphStorage graphStorage;
    };

    struct GeneralHistos
    {
        TH1D *packetTimestamps = nullptr;
        TH1D *eventsPerPacket = nullptr;
        TH1D *eventTimestamps = nullptr;
        TH1D *fullTimestamps = nullptr;

        TH1D *packetTimestampDeltas = nullptr;
        TH1D *eventTimestampDeltas = nullptr;
        TH1D *fullTimestampDeltas = nullptr;

        std::optional<uint64_t> lastPacketTimestamp;
        std::optional<uint64_t> lastEventTimestamp;
        std::optional<uint64_t> lastFullTimestamp;

        // Used to create value-over-time graphs at the end of a run.
        // These can grow indefinitely. Makes the OOM-killer happy.
        struct GraphStorage
        {
            std::vector<double> packetTimestamps; // packet stamp, aka buffer stamp
            std::vector<double> eventTimestamps;  // the relative timestamp transmitted with each event
            std::vector<double> fullTimestamps;   // calculated full event stamp: packet stamp + event stamp
        };

        GraphStorage graphStorage;
    };

    // The ROOT ouptut file.
    std::unique_ptr<TFile> histoOutFile;

    // For MCPD data.
    std::unique_ptr<McpdHistos> mcpdHistos;

    // For MDLL data. Indexed by MDLL device id.
    std::vector<std::unique_ptr<MdllHistos>> mdllHistos;

    // General histograms indexed by device id (MCPD or MDLL).
    std::vector<std::unique_ptr<GeneralHistos>> generalHistos;

    // Set to true if value over time graphs should be created. Eats memory and
    // will crash on long runs.
    bool enableGraphs = false;

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
    return get_histo(ctx.mcpdHistos->amplitudes, mcpdId, mpsdId, channel);
}

inline TH1D *get_position_histo(RootHistoContext &ctx, unsigned mcpdId, unsigned mpsdId, unsigned channel)
{
    return get_histo(ctx.mcpdHistos->positions, mcpdId, mpsdId, channel);
}

inline TH1D *get_timestamp_histo(RootHistoContext &ctx, unsigned mcpdId, unsigned mpsdId, unsigned channel)
{
    return get_histo(ctx.mcpdHistos->timestamps, mcpdId, mpsdId, channel);
}

}

#endif /* __MCPD_ROOT_HISTOS_H__ */
