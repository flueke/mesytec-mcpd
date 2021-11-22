#include <iostream>
#include <mesytec-mcpd/mesytec-mcpd.h>

using namespace mesytec::mcpd;
using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
    std::error_code ec = {};

    int mcpdCommandSocket = connect_udp_socket("192.168.168.121", McpdDefaultPort, &ec);

    if (ec)
    {
        cerr << "Error connecting to mcpd: " << ec.message() << std::endl;
        return 1;
    }

    unsigned mcpdId = 0u;

    McpdVersionInfo vi = {};

    ec = mcpd_get_version(mcpdCommandSocket, mcpdId, vi);

    if (ec)
    {
        cerr << "Error reading MCPD version info: " << ec.message() << std::endl;
        return 1;
    }

    cout << "MCPD version info: CPU=" << vi.cpu[0] << "." << vi.cpu[1]
        << ", FPGA=" << vi.fpga[0] << "." << vi.fpga[1] << endl;

    return 0;
}
