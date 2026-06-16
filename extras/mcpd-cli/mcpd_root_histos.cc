#include "mcpd_root_histos.h"
#include <spdlog/spdlog.h>
#include <TGraph.h>

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

RootHistoContext::MdllHistos* get_or_create_mdll_histos(
    TFile *outfile,
    std::vector<std::unique_ptr<RootHistoContext::MdllHistos>> &mdllHistos,
    unsigned mdllId)
{
    namespace mn = event_constants::mdll_neutron;

    if (mdllId < mdllHistos.size() && mdllHistos[mdllId])
    {
        spdlog::trace("Returning existing histograms for MDLL {}", mdllId);
        return mdllHistos[mdllId].get();
    }

    spdlog::debug("No mdll histos yet, creating for MDLL {}", mdllId);

    mdllHistos.resize(std::max(static_cast<size_t>(mdllId + 1), mdllHistos.size()));

    if (!mdllHistos[mdllId])
    {
        mdllHistos[mdllId] = std::make_unique<RootHistoContext::MdllHistos>();
        auto histos = mdllHistos[mdllId].get();

        if (auto dir = outfile->mkdir(fmt::format("mdll{}", mdllId).c_str(), "", true))
        {
            dir->cd();
        }
        else
        {
            spdlog::error("Failed to create MDLL directory for id {}", mdllId);
            return {};
        }

        histos->amplitudes = new TH1D("mdll_amplitudes", fmt::format("MDLL{} Amplitudes", mdllId).c_str(),
            1u << mn::AmplitudeBits, 0, (1u << mn::AmplitudeBits) + 1.0);

        histos->xPositions = new TH1D("mdll_xPositions", fmt::format("MDLL{} X Positions", mdllId).c_str(),
            1u << mn::xPosBits, 0, (1u << mn::xPosBits) + 1.0);

        histos->yPositions = new TH1D("mdll_yPositions", fmt::format("MDLL{} Y Positions", mdllId).c_str(),
            1u << mn::yPosBits, 0, (1u << mn::yPosBits) + 1.0);

        histos->xyPositions = new TH2D("mdll_xyPositions", fmt::format("MDLL{} XY Positions", mdllId).c_str(),
            1u << mn::xPosBits, 0, (1u << mn::xPosBits) + 1.0,
            1u << mn::yPosBits, 0, (1u << mn::yPosBits) + 1.0);

        spdlog::info("Created histograms for MDLL {}: amplitude, xPosition, yPosition, xyPosition, ...", mdllId);
    }

    outfile->cd();
    return mdllHistos[mdllId].get();
}

RootHistoContext::GeneralHistos* get_or_create_general_histos(
    TFile *outfile,
    std::vector<std::unique_ptr<RootHistoContext::GeneralHistos>> &generalHistos,
    unsigned deviceId)
{
    if (deviceId < generalHistos.size() && generalHistos[deviceId])
    {
        spdlog::trace("Returning existing general histograms for device {}", deviceId);
        return generalHistos[deviceId].get();
    }

    spdlog::debug("No general histos yet, creating for device {}", deviceId);

    generalHistos.resize(std::max(static_cast<size_t>(deviceId + 1), generalHistos.size()));

    assert(deviceId < generalHistos.size());

    if (!generalHistos[deviceId])
    {
        generalHistos[deviceId] = std::make_unique<RootHistoContext::GeneralHistos>();
        auto histos = generalHistos[deviceId].get();

        if (auto dir = outfile->mkdir(fmt::format("device{}", deviceId).c_str(), "", true))
        {
            dir->cd();
        }
        else
        {
            spdlog::error("Failed to create directory for device {}", deviceId);
            return {};
        }

        // timestamp and events per packet histos
        histos->packetTimestamps = new TH1D("packetTimestamps", fmt::format("Device{} Packet Timestamps", deviceId).c_str(),
            20, 0, 1llu << 48);

        histos->eventTimestamps = new TH1D("eventTimestamps", fmt::format("Device{} Event Timestamps", deviceId).c_str(),
            1u << event_constants::TimestampBits, 0, (1u << event_constants::TimestampBits) + 1.0);

        histos->fullTimestamps = new TH1D("fullTimestamps", fmt::format("Device{} Full Timestamps", deviceId).c_str(),
            20, 0, 1llu << 48);

        histos->eventsPerPacket = new TH1D("eventsPerPacket", fmt::format("Device{} Events per Packet", deviceId).c_str(),
            1u << 8, 0, (1u << 8) + 1.0);

        // delta histos
        static const size_t deltaHistoBins = 1u << 16;
        static const double deltaHistoMin = - (1u << 16) * 0.5;
        static const double deltaHistoMax = + (1u << 16) * 0.5;

        histos->packetTimestampDeltas = new TH1D("packetTimestampDeltas", fmt::format("Device{} Packet Timestamp Deltas", deviceId).c_str(),
            deltaHistoBins, deltaHistoMin, deltaHistoMax);

        histos->eventTimestampDeltas = new TH1D("eventTimestampDeltas", fmt::format("Device{} Event Timestamp Deltas", deviceId).c_str(),
            deltaHistoBins, deltaHistoMin, deltaHistoMax);

        histos->fullTimestampDeltas = new TH1D("fullTimestampDeltas", fmt::format("Device{} Full Timestamp Deltas", deviceId).c_str(),
            deltaHistoBins, deltaHistoMin, deltaHistoMax);

        spdlog::info("Created general histograms for device {}", deviceId);
    }

    outfile->cd();
    return generalHistos[deviceId].get();
}

void root_histos_process_packet(RootHistoContext &ctx, const DataPacket &packet)
{
    const auto packetTimestamp = get_header_timestamp(packet);
    const auto eventCount = get_event_count(packet);

    auto generalHistos = get_or_create_general_histos(ctx.histoOutFile.get(), ctx.generalHistos, packet.deviceId);
    generalHistos->packetTimestamps->Fill(packetTimestamp);
    generalHistos->eventsPerPacket->Fill(eventCount);

    RootHistoContext::MdllHistos *mdllHistos = nullptr;

    if (packet.bufferType == MdllDataBufferType)
        mdllHistos = get_or_create_mdll_histos(ctx.histoOutFile.get(), ctx.mdllHistos, packet.deviceId);

    if (generalHistos->lastPacketTimestamp)
    {
        auto delta = packetTimestamp - *generalHistos->lastPacketTimestamp;
        generalHistos->packetTimestampDeltas->Fill(delta);
    }

    generalHistos->lastPacketTimestamp = packetTimestamp;

    if (ctx.enableGraphs)
    {
        generalHistos->graphStorage.packetTimestamps.push_back(packetTimestamp);
    }

    for(size_t ei=0; ei<eventCount; ++ei)
    {
        auto event = decode_event(packet, ei);

        generalHistos->eventTimestamps->Fill(event.event_timestamp);
        generalHistos->fullTimestamps->Fill(event.timestamp);

        if (generalHistos->lastEventTimestamp)
        {
            auto delta = event.event_timestamp - *generalHistos->lastEventTimestamp;
            generalHistos->eventTimestampDeltas->Fill(delta);
        }

        if (generalHistos->lastFullTimestamp)
        {
            auto delta = event.timestamp - *generalHistos->lastFullTimestamp;
            generalHistos->fullTimestampDeltas->Fill(delta);
        }

        generalHistos->lastEventTimestamp = event.event_timestamp;
        generalHistos->lastFullTimestamp = event.timestamp;

        if (ctx.enableGraphs)
        {
            generalHistos->graphStorage.eventTimestamps.push_back(event.event_timestamp);
            generalHistos->graphStorage.fullTimestamps.push_back(event.timestamp);
        }

        if (event.type == EventType::Neutron)
        {
            auto histoAmp = get_maybe_create(
                ctx.histoOutFile.get(), ctx.mcpdHistos->amplitudes,
                packet, event, 1u << 10, "amplitude");

            auto histoPos = get_maybe_create(
                ctx.histoOutFile.get(), ctx.mcpdHistos->positions,
                packet, event, 1u << 10, "position");

            auto histoTimestamp = get_maybe_create(
                ctx.histoOutFile.get(), ctx.mcpdHistos->timestamps,
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
            assert(mdllHistos);
            if (!mdllHistos)
            {
                spdlog::error("MDLL neutron event but no histograms for MDLL {}. This should not happen.", packet.deviceId);
                continue;
            }

            if (mdllHistos->amplitudes)
                mdllHistos->amplitudes->Fill(event.mdllNeutron.amplitude);
            if (mdllHistos->xPositions)
                mdllHistos->xPositions->Fill(event.mdllNeutron.xPos);
            if (mdllHistos->yPositions)
                mdllHistos->yPositions->Fill(event.mdllNeutron.yPos);
            if (mdllHistos->xyPositions)
                mdllHistos->xyPositions->Fill(event.mdllNeutron.xPos, event.mdllNeutron.yPos);

            if (ctx.enableGraphs)
            {
                mdllHistos->graphStorage.amplitudes.push_back(event.mdllNeutron.amplitude);
                mdllHistos->graphStorage.xPositions.push_back(event.mdllNeutron.xPos);
                mdllHistos->graphStorage.yPositions.push_back(event.mdllNeutron.yPos);
            }
        }
        else if (event.type == EventType::Trigger)
        {
            // TODO: maybe do add some trigger specific histograms or graphs.
        }
    }
}

void root_histos_finalize(RootHistoContext &ctx)
{
    // general
    for (size_t deviceId = 0; deviceId < ctx.generalHistos.size(); ++deviceId)
    {
        auto histos = ctx.generalHistos[deviceId].get();

        if (!histos)
        {
            // can happen if we have a hole in the id ranges, e.g. device0, then
            // device2, so device1 will not have any histos created
            continue;
        }

        if (histos->graphStorage.fullTimestamps.empty())
        {
            // did not get any events from this device
            continue;
        }

        if (auto dir = ctx.histoOutFile->mkdir(fmt::format("device{}", deviceId).c_str(), "", true))
        {
            dir->cd();
        }

        auto graphPacketTimestamps = new TGraph(histos->graphStorage.packetTimestamps.size(), histos->graphStorage.packetTimestamps.data());
        graphPacketTimestamps->SetName("packet_timestamps_in_recv_order");
        graphPacketTimestamps->SetTitle(fmt::format("Device{} Packet Timestamps in Receive Order;Recv Order;Packet Timestamp", deviceId).c_str());
        graphPacketTimestamps->Write("", TObject::kOverwrite);

        auto graphEventTimestamps = new TGraph(histos->graphStorage.eventTimestamps.size(), histos->graphStorage.eventTimestamps.data());
        graphEventTimestamps->SetName("event_timestamps_in_recv_order");
        graphEventTimestamps->SetTitle(fmt::format("Device{} Event Timestamps in Receive Order;Recv Order;Event Timestamp", deviceId).c_str());
        graphEventTimestamps->Write("", TObject::kOverwrite);

        auto graphFullTimestamps = new TGraph(histos->graphStorage.fullTimestamps.size(), histos->graphStorage.fullTimestamps.data());
        graphFullTimestamps->SetName("full_timestamps_in_recv_order");
        graphFullTimestamps->SetTitle(fmt::format("Device{} Full Timestamps in Receive Order;Recv Order;Full Timestamp", deviceId).c_str());
        graphFullTimestamps->Write("", TObject::kOverwrite);

        spdlog::info("Wrote graphs for device {} with {} points", deviceId, histos->graphStorage.fullTimestamps.size());
    }

    // mdll
    for (size_t mdllId = 0; mdllId < ctx.mdllHistos.size(); ++mdllId)
    {
        auto mdllHistos = ctx.mdllHistos[mdllId].get();
        auto generalHistos = ctx.generalHistos[mdllId].get();

        if (!mdllHistos || !generalHistos)
        {
            // can happen if we have a hole in the id ranges, e.g. mdll0, then
            // mdll2, so mdll1 will not have any histos created
            continue;
        }

        if (mdllHistos->graphStorage.amplitudes.empty())
        {
            // did not receive any events from this mdll
            continue;
        }

        if (auto dir = ctx.histoOutFile->mkdir(fmt::format("mdll{}", mdllId).c_str(), "", true))
        {
            dir->cd();
        }

        auto graphAmplitude = new TGraph(
            generalHistos->graphStorage.fullTimestamps.size(),
            generalHistos->graphStorage.fullTimestamps.data(),
            mdllHistos->graphStorage.amplitudes.data());

        graphAmplitude->SetName(fmt::format("mdll{}_amplitude_over_time", mdllId).c_str());
        graphAmplitude->SetTitle(fmt::format("MDLL{} Amplitude over Time;Timestamp;Amplitude", mdllId).c_str());
        graphAmplitude->Write("", TObject::kOverwrite);

        auto graphXPos = new TGraph(
            generalHistos->graphStorage.fullTimestamps.size(),
            generalHistos->graphStorage.fullTimestamps.data(),
            mdllHistos->graphStorage.xPositions.data());
        graphXPos->SetName(fmt::format("mdll{}_xpos_over_time", mdllId).c_str());
        graphXPos->SetTitle(fmt::format("MDLL{} X Position over Time;Timestamp;X Position", mdllId).c_str());
        graphXPos->Write("", TObject::kOverwrite);

        auto graphYPos = new TGraph(
            generalHistos->graphStorage.fullTimestamps.size(),
            generalHistos->graphStorage.fullTimestamps.data(),
            mdllHistos->graphStorage.yPositions.data());
        graphYPos->SetName(fmt::format("mdll{}_ypos_over_time", mdllId).c_str());
        graphYPos->SetTitle(fmt::format("MDLL{} Y Position over Time;Timestamp;Y Position", mdllId).c_str());
        graphYPos->Write("", TObject::kOverwrite);

        spdlog::info("Wrote graphs for MDLL {} with {} points", mdllId, generalHistos->graphStorage.fullTimestamps.size());
    }

    ctx.histoOutFile->Write("", TObject::kOverwrite);
}

}
