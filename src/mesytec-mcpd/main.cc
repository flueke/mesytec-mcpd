#include <ios>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <thread>
#include <fstream>

#ifndef __WIN32
#include <netinet/ip.h> // sockaddr_in
#else
#include <winsock2.h>
#endif

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

#include <iostream>

#include "mesytec-mcpd.h"

using namespace mesytec::mcpd;
using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::trace);

    //
    // socket setup
    //
    std::error_code ec;

    static const char *mcpdIpAddress = "192.168.43.42";
    u8 mcpdId = 0u;

    int cmdSock = connect_udp_socket(mcpdIpAddress, DefaultMcpdPort, &ec);

    if (cmdSock < 0)
    {
        spdlog::error("Error creating command socket: {}", ec.message());
        return 1;
    }

    int dataSock = connect_udp_socket(mcpdIpAddress, DefaultMcpdPort+1, &ec);

    if (dataSock < 0)
    {
        spdlog::error("Error creating data socket: {}", ec.message());
        return 1;
    }

    u16 localDataPort = get_local_socket_port(dataSock);

    spdlog::info("localDataPort={}, 0x{:04x}", localDataPort, localDataPort);

    if ((ec = mcpd_set_data_dest_port(cmdSock, mcpdId, localDataPort)))
    {
        spdlog::error("Error setting MCPD data destination port: {}", ec.message());
        return 1;
    }

#if 0
    if (auto ec = mcpd_set_ip_address(
        cmdSock,
        mcpdId,
        "192.168.43.42"))
        throw ec;

    return 0;
#endif

#if 1
    McpdVersionInfo versionInfo = {};

    if (auto ec = mcpd_get_version(cmdSock, mcpdId, versionInfo))
        throw ec;


    cout << "cpu major=" << versionInfo.cpu[0] << endl;
    cout << "cpu minor=" << versionInfo.cpu[1] << endl;
    cout << "fpga major=" << static_cast<u16>(versionInfo.fpga[0]) << endl;
    cout << "fpga minor=" << static_cast<u16>(versionInfo.fpga[1]) << endl;
    cout << endl;
#endif


#if 0
    // XXX: Need to carry the mcpdId around and update it on each change
    u8 mcpdNewId = 42u;
    if (auto ec = mcpd_set_id(cmdSock, mcpdId, mcpdNewId))
        throw ec;
    mcpdId = mcpdNewId;
#endif
#if 0

    if (auto ec = mcpd_stop_daq(cmdSock, mcpdId))
        throw ec;
#endif

#if 1

    for (u8 mpsdId=0; mpsdId<2; ++mpsdId)
    {

        // sets the gain for all channels
#if 1
        if (auto ec = mpsd_set_gain(
                cmdSock, mcpdId,
                mpsdId, 8, 200))
            throw ec;
#endif

#if 1
        // threshold
        if (auto ec = mpsd_set_threshold(
                cmdSock, mcpdId, mpsdId, 255))
            throw ec;

#endif

#if 0
        // enable pulser for all channels
        for (u8 channel = 0; channel < 8; ++channel)
        {
            u8 amplitude = 255;

            if (auto ec = mpsd_set_pulser(
                    cmdSock, mcpdId, mpsdId, channel, ChannelPosition::Center,
                    amplitude, PulserState::On))
                throw ec;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

#endif

    return 0;

    if (auto ec = mcpd_start_daq(cmdSock, mcpdId))
        throw ec;

    cout << "daq started, writing data to ./mcpd.lst" << endl;
    std::ofstream lstOut("./mcpd.list");
    //getchar();

    const int PacketsToReceive = 10000;

    for (int i=0; i<PacketsToReceive; ++i)
    {
        DataPacket dataPacket = {};

        size_t bytesTransferred = 0u;
        if (auto ec = receive_one_packet(
                dataSock,
                reinterpret_cast<u8 *>(&dataPacket),
                sizeof(dataPacket),
                bytesTransferred,
                DefaultReadTimeout_ms))
        {
            spdlog::error("data packet read loop: {}", ec.message());
        }
        else
        {
            //spdlog::trace("data packet #{}: {}", i, to_string(dataPacket));
            lstOut.write(reinterpret_cast<const char *>(&dataPacket), sizeof(dataPacket));
        }
    }

    if (auto ec = mcpd_stop_daq(cmdSock, mcpdId))
        throw ec;

    return 0;
}
