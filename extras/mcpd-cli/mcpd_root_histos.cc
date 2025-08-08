#include "mcpd_root_histos.h"
#include <spdlog/spdlog.h>

namespace mesytec
{
namespace mcpd
{

RootHistoContext::~RootHistoContext()
{
    if (histoOutFile && histoOutFile->IsOpen())
    {
        spdlog::debug("Closing histo file {}", histoOutFile->GetName());
        histoOutFile->Write("", TObject::kOverwrite);
    }
}

RootHistoContext create_histo_context(const std::string &outputFilename)
{
    RootHistoContext result = {};

    result.histoOutFile = std::make_unique<TFile>(outputFilename.c_str(), "recreate");

    if (result.histoOutFile->IsZombie() || !result.histoOutFile->IsOpen())
    {
        throw std::runtime_error(fmt::format(
                "Error opening histo output file '{}': {}",
                outputFilename.c_str(),
                strerror(result.histoOutFile->GetErrno())));
    }

    namespace mn = event_constants::mdll_neutron;

    result.mdll_amplitudes = new TH1D("mdll_amplitudes", "MDLL Amplitudes",
        1u << mn::AmplitudeBits, 0, (1u << mn::AmplitudeBits) + 1.0);


    result.mdll_xPositions = new TH1D("mdll_xPositions", "MDLL X Positions",
        1u << mn::xPosBits, 0, (1u << mn::xPosBits) + 1.0);

    result.mdll_yPositions = new TH1D("mdll_yPositions", "MDLL Y Positions",
        1u << mn::yPosBits, 0, (1u << mn::yPosBits) + 1.0);

    result.mdll_xyPositions = new TH2D("mdll_xyPositions", "MDLL XY Positions",
        1u << mn::xPosBits, 0, (1u << mn::xPosBits) + 1.0,
        1u << mn::yPosBits, 0, (1u << mn::yPosBits) + 1.0);

    return result;
}

TH1D *get_maybe_create(
    TFile *outfile,
    std::vector<TH1D *> &histos,
    unsigned mcpdId, unsigned mpsdId, unsigned channel,
    unsigned bins, const char *name)
{
    auto idx = linear_address(mcpdId, mpsdId, channel);

    if (idx < histos.size())
        return histos[idx];


    if (auto dir = outfile->mkdir(fmt::format("mcpd{}", mcpdId).c_str(), "", true))
    {
        if ((dir = dir->mkdir(fmt::format("mpsd{}", mpsdId).c_str(), "", true)))
        {
            dir->cd();

            auto histoname = fmt::format(
                "mcpd{}_mpsd{}_channel{}_{}",
                mcpdId, mpsdId, channel, name);

            auto histo = std::make_unique<TH1D>(
                histoname.c_str(),
                histoname.c_str(),
                bins,
                0.0,
                bins + 1.0);

            histos.resize(idx+1);
            histos[idx] = histo.get();
            outfile->cd();
            return histo.release();
        }
    }

    outfile->cd();
    return nullptr;
}

TH1D *get_maybe_create(
    TFile *outfile,
    std::vector<TH1D *> &histos,
    const DataPacket &packet,
    const DecodedEvent &event,
    unsigned bins, const char *name)
{
    if (event.type == EventType::Neutron)
        return get_maybe_create(
            outfile, histos,
            packet.deviceId,
            event.neutron.mpsdId, event.neutron.channel,
            bins, name);
    return nullptr;
}

void root_histos_process_packet(RootHistoContext &ctx, const DataPacket &packet)
{
    const auto eventCount = get_event_count(packet);

    for(size_t ei=0; ei<eventCount; ++ei)
    {
        auto event = decode_event(packet, ei);

        if (event.type == EventType::Neutron)
        {
            auto histoAmp = get_maybe_create(
                ctx.histoOutFile.get(), ctx.amplitudes,
                packet, event, 1u << 10, "amplitude");

            auto histoPos = get_maybe_create(
                ctx.histoOutFile.get(), ctx.positions,
                packet, event, 1u << 10, "position");

            auto histoTimestamp = get_maybe_create(
                ctx.histoOutFile.get(), ctx.timestamps,
                packet, event, 1u << 19, "timestamp");

            if (histoAmp)
                histoAmp->Fill(event.neutron.amplitude);

            if (histoPos)
                histoPos->Fill(event.neutron.position);

            if (histoTimestamp)
                histoTimestamp->Fill(event.timestamp);
        }
        else if (event.type == EventType::MdllNeutron)
        {
            if (ctx.mdll_amplitudes)
                ctx.mdll_amplitudes->Fill(event.mdllNeutron.amplitude);
            if (ctx.mdll_xPositions)
                ctx.mdll_xPositions->Fill(event.mdllNeutron.xPos);
            if (ctx.mdll_yPositions)
                ctx.mdll_yPositions->Fill(event.mdllNeutron.yPos);
            if (ctx.mdll_xyPositions)
                ctx.mdll_xyPositions->Fill(event.mdllNeutron.xPos, event.mdllNeutron.yPos);
        }
    }
}

}
}
