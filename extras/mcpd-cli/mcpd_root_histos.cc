#include "mcpd_root_histos.h"
#include <spdlog/spdlog.h>

namespace mesytec::mcpd
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

RootHistoContext::MdllHistos get_or_create_mdll_histos(
    TFile *outfile,
    std::vector<RootHistoContext::MdllHistos> &mdllHistos,
    unsigned mdllId)
{
    namespace mn = event_constants::mdll_neutron;

    if (mdllId < mdllHistos.size() && mdllHistos[mdllId].amplitudes)
        return mdllHistos[mdllId];

    mdllHistos.resize(mdllId + 1);

    auto &histos = mdllHistos[mdllId];

    if (!histos.amplitudes)
    {
        if (auto dir = outfile->mkdir(fmt::format("mdll{}", mdllId).c_str(), "", true))
        {
            dir->cd();
        }
        else
        {
            spdlog::error("Failed to create MDLL directory for id {}", mdllId);
            return {};
        }

        histos.amplitudes = new TH1D("mdll_amplitudes", fmt::format("MDLL{} Amplitudes", mdllId).c_str(),
            1u << mn::AmplitudeBits, 0, (1u << mn::AmplitudeBits) + 1.0);

        histos.xPositions = new TH1D("mdll_xPositions", fmt::format("MDLL{} X Positions", mdllId).c_str(),
            1u << mn::xPosBits, 0, (1u << mn::xPosBits) + 1.0);

        histos.yPositions = new TH1D("mdll_yPositions", fmt::format("MDLL{} Y Positions", mdllId).c_str(),
            1u << mn::yPosBits, 0, (1u << mn::yPosBits) + 1.0);

        histos.xyPositions = new TH2D("mdll_xyPositions", fmt::format("MDLL{} XY Positions", mdllId).c_str(),
            1u << mn::xPosBits, 0, (1u << mn::xPosBits) + 1.0,
            1u << mn::yPosBits, 0, (1u << mn::yPosBits) + 1.0);
    }

    outfile->cd();
    return histos;
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
            auto mdllHistos = get_or_create_mdll_histos(
                ctx.histoOutFile.get(), ctx.mdllHistos, packet.deviceId);

            if (mdllHistos.amplitudes)
                mdllHistos.amplitudes->Fill(event.mdllNeutron.amplitude);
            if (mdllHistos.xPositions)
                mdllHistos.xPositions->Fill(event.mdllNeutron.xPos);
            if (mdllHistos.yPositions)
                mdllHistos.yPositions->Fill(event.mdllNeutron.yPos);
            if (mdllHistos.xyPositions)
                mdllHistos.xyPositions->Fill(event.mdllNeutron.xPos, event.mdllNeutron.yPos);
        }
    }
}

}
