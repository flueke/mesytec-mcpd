#include "mcpd_py_lib.h"
#include <gtest/gtest.h>
#include <mesytec-mcpd/util/logging.h>
#include <pybind11/embed.h> // for py::scoped_interpreter

namespace py = pybind11;

using namespace mesytec::mcpd;
using namespace mesytec::mcpd::py_lib;

// Note: always using a non-default port to _not_ receive data from MCPDs that have been left
// running :) Real programmers (!) would abstract the socket out and run with a mock socket instead
// I guess.

// Global Python interpreter for all tests
class PythonEnvironment: public ::testing::Environment
{
  public:
    PythonEnvironment()
    {
        set_global_log_level(spdlog::level::trace);
        spdlog::set_level(spdlog::level::trace);
    }
    ~PythonEnvironment() override {}

    void SetUp() override { guard = std::make_unique<py::scoped_interpreter>(); }

    void TearDown() override { guard.reset(); }

  private:
    std::unique_ptr<py::scoped_interpreter> guard;
};

// Register the environment
[[maybe_unused]] static ::testing::Environment *const python_env =
    ::testing::AddGlobalTestEnvironment(new PythonEnvironment());

TEST(mcpd_py_lib, QueueAugPackets)
{
    // This import is needed to make the pybind11 casts work. Otherwise the
    // types are not known and runtime conversion error occur.
    auto mcpd_lib = py::module_::import("_mesytec_mcpd_py");
    py::object Queue = py::module_::import("queue").attr("Queue");
    auto queue_ = Queue(10);
    AugmentedDataPacket augPacket = {};
    augPacket.packet.runId = 42;

    queue_.attr("put")(py::cast(augPacket, py::return_value_policy::copy), false);
    ASSERT_EQ(queue_.attr("qsize")().cast<size_t>(), 1u);

    auto augCopy = queue_.attr("get")().cast<AugmentedDataPacket>();
    ASSERT_EQ(queue_.attr("qsize")().cast<size_t>(), 0u);
    ASSERT_EQ(augCopy.packet.runId, 42u);
}

TEST(mcpd_py_lib, CreateDestroy)
{
    for (size_t i = 0; i < 10; ++i)
    {
        Readout readout(McpdDefaultPort + 3);

        ASSERT_FALSE(readout.isRunning());
        ASSERT_FALSE(readout.hasException());
        ASSERT_TRUE(readout.getQueue().attr("empty")().cast<bool>());
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
        ASSERT_FALSE(readout.hasException());
        ASSERT_TRUE(readout.getQueue().attr("empty")().cast<bool>());
        ASSERT_EQ(readout.getCounters().packets, 0u);

        readout.stop();
        ASSERT_FALSE(readout.isRunning());
        ASSERT_FALSE(readout.hasException());
        ASSERT_TRUE(readout.getQueue().attr("empty")().cast<bool>());
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

    ASSERT_FALSE(readout.hasException());
    ASSERT_FALSE(readout1.hasException());

    // This will throw 'address already in use'.
    ASSERT_THROW(readout1.start(), std::system_error);

    ASSERT_TRUE(readout.isRunning());
    ASSERT_FALSE(readout1.isRunning());

    // No readout exception should be set. It's only done if startup
    // succeeded and then during readout time an error occurs.
    ASSERT_FALSE(readout.hasException());
    ASSERT_FALSE(readout1.hasException());
}
