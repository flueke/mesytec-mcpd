#include <gtest/gtest.h>
#include <mesytec-mcpd/util/logging.h>
#include "mcpd_py_lib.h"

using namespace mesytec::mcpd;

// Note: always using a non-default port to _not_ receive data from MCPDs that have been left running :)
// Real programmers (!) would abstract the socket out and run with a mock socket instead I guess.

TEST(mcpd_py_lib, CreateDestroyBeHappy)
{
    set_global_log_level(spdlog::level::trace);
    spdlog::set_level(spdlog::level::trace);

    for (size_t i = 0; i < 10; ++i)
    {
        Readout readout(McpdDefaultPort + 3);

        ASSERT_FALSE(readout.isRunning());
        ASSERT_FALSE(readout.hasReadoutException());
        ASSERT_TRUE(readout.getPackets().empty());
        ASSERT_EQ(readout.getCounters().packets, 0u);
    }
}

TEST(mcpd_py_lib, StartStopRestart)
{
        Readout readout(McpdDefaultPort + 3);

        for (size_t i = 0; i < 10; ++i)
        {
            readout.start();
            ASSERT_TRUE(readout.isRunning());
            ASSERT_FALSE(readout.hasReadoutException());
            ASSERT_TRUE(readout.getPackets().empty());
            ASSERT_EQ(readout.getCounters().packets, 0u);

            readout.stop();
            ASSERT_FALSE(readout.isRunning());
            ASSERT_FALSE(readout.hasReadoutException());
            ASSERT_TRUE(readout.getPackets().empty());
            ASSERT_EQ(readout.getCounters().packets, 0u);
        }
}

TEST(mcpd_py_lib, StartMultiple)
{
        Readout readout(McpdDefaultPort + 3);
        Readout readout1(McpdDefaultPort + 3);

        readout.start();

        ASSERT_TRUE(readout.isRunning());
        ASSERT_FALSE(readout1.isRunning());

        ASSERT_FALSE(readout.hasReadoutException());
        ASSERT_FALSE(readout1.hasReadoutException());

        // This will throw 'address already in use'.
        ASSERT_THROW(readout1.start(), std::system_error);

        ASSERT_TRUE(readout.isRunning());
        ASSERT_FALSE(readout1.isRunning());

        // No readout exception should be set. It's only done if startup
        // succeeded and then during readout time an error occurs.
        ASSERT_FALSE(readout.hasReadoutException());
        ASSERT_FALSE(readout1.hasReadoutException());
}
