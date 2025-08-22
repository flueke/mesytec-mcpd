#include <iostream>
#include <mesytec-mcpd/mesytec-mcpd.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <cassert>

using namespace mesytec::mcpd;

// Note: little endian only code. qmesydaq writes out big endian data so the
// swaps and data packet buffer fixup here are for little endian platforms.

static const u64 HeaderSeparator = 0xffffaaaa55550000;
static const u64 PacketSeparator = 0xaaaa5555ffff0000;

// The size of the static part of a data packet in bytes. The actual size of
// the data packet depends on the value of the bufferLength field.
const static auto MinimumDataPacketSize = sizeof(DataPacket) - sizeof(DataPacket::data);

u16 byteSwap(const u16 v)
{
    u16 lo = v & 0x00FF;
    u16 hi = (v & 0xFF00) >> 8;
    return (lo << 8) | hi;
}

bool skip_mdat_header(std::ifstream &inFile)
{
    std::vector<u8> buffer;
    buffer.resize(64 * 1024);

    inFile.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
    buffer.resize(inFile.gcount());

    if (buffer.size() < sizeof(HeaderSeparator))
    {
        std::cerr << "Input file is too small or corrupted.\n";
        return false;
    }

    // Find the separator in the buffer
    size_t separatorPos = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i <= buffer.size() - sizeof(HeaderSeparator); ++i)
    {
        u64 value = *reinterpret_cast<const u64 *>(buffer.data() + i);

        if (value == HeaderSeparator)
        {
            separatorPos = i;
            spdlog::debug("Found HeaderSeparator {:#018x} at file position {}", HeaderSeparator, separatorPos);
            break;
        }
    }

    if (separatorPos == std::numeric_limits<size_t>::max())
    {
        std::cerr << "HeaderSeparator not found in buffer.\n";
        return false;
    }

    inFile.seekg(separatorPos + sizeof(HeaderSeparator), std::ios::beg);
    return true;
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);
    if (argc < 3)
    {
        std::cerr << "Usage: mcpd-cli <input-file.mdat> <output-file.mcpdlst>\n";
        return 1;
    }

    std::string inputFilename = argv[1];
    std::string outputFilename = argv[2];

    std::ifstream inFile;
    inFile.exceptions(std::ifstream::badbit);
    std::ofstream outFile;
    outFile.exceptions(std::ifstream::badbit);

    try
    {
        inFile.open(inputFilename, std::ios::in | std::ios::binary);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error opening input file: " << e.what() << "\n";
        return 1;
    }

    if (!skip_mdat_header(inFile))
    {
        std::cerr << "Failed to skip over the mdat header data.\n";
        return 1;
    }

    try
    {
        outFile.open(outputFilename, std::ios::out | std::ios::binary);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error opening output file: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Skipped mdat header, starting to read data packets...\n";

    size_t totalDataPackets = 0;
    int retval = 0;

    while (true)
    {
        DataPacket dataPacket = {};

        try
        {
            if (inFile.eof())
                break;

            inFile.read(reinterpret_cast<char *>(&dataPacket), sizeof(dataPacket));
            auto bytesRead = inFile.gcount();

            if (bytesRead < MinimumDataPacketSize)
            {
                spdlog::debug("Read less than the minimum data packet size (wanted={}, got={}), stopping reading.",
                     MinimumDataPacketSize, bytesRead);
                break;
            }

            // byteswap the PacketBase fields
            dataPacket.bufferLength = byteSwap(dataPacket.bufferLength);
            dataPacket.bufferType = byteSwap(dataPacket.bufferType);
            dataPacket.headerLength = byteSwap(dataPacket.headerLength);
            dataPacket.bufferNumber = byteSwap(dataPacket.bufferNumber);

            auto dataLen = get_data_length(dataPacket);

            // Fixup the byte order here (measurement.cpp:1288).
            char *pD = reinterpret_cast<char *>(&dataPacket.runId);
            for (int i = 0; i < dataLen; i += 2)
            {
                std::swap(pD[i], pD[i + 1]);
            }

            auto bytesUsed = MinimumDataPacketSize + dataLen * sizeof(u16);
            auto trailingBytes = bytesRead - bytesUsed; // we read this many bytes too much
            auto eventCount = get_event_count(dataPacket);

            spdlog::trace("Read DataPacket: bytesRead={}, bytesUsed={}, trailingBytes={}, dataLen={}, eventCount={}: {}",
                          bytesRead, bytesUsed, trailingBytes, dataLen, eventCount, to_string(dataPacket));
            ++totalDataPackets;

            // zero out the unused data words in the data packet in case we read too much data before
            for (size_t i = dataLen; i < DataPacketMaxDataWords; ++i)
            {
                dataPacket.data[i] = 0;
            }

            outFile.write(reinterpret_cast<const char *>(&dataPacket), sizeof(dataPacket));

            // In case we read more data than we should have, we have to seek
            // backwards to land on the inter packet separator.
            auto curPos = inFile.tellg();

            if (curPos >= 0)
            {
                inFile.seekg(-trailingBytes, std::ios::cur);
            }
            else
            {
                inFile.clear(); // clear the fail state from trying to read beyond eof
                inFile.seekg(-trailingBytes, std::ios::end);
            }

            auto correctedPos = inFile.tellg();

            spdlog::trace("trailingBytes={}, Position in input file before correction={}, after correction={}, delta={}",
                          trailingBytes, curPos, correctedPos, curPos - correctedPos);

            u64 sep = {};
            inFile.read(reinterpret_cast<char *>(&sep), sizeof(sep));
            bytesRead = inFile.gcount();

            if (bytesRead < sizeof(sep))
            {
                spdlog::error("Read less than the size of a separator ({} bytes), stopping read.", bytesRead);
                retval = 1;
                break;
            }

            if (sep != PacketSeparator)
            {
                spdlog::error("Expected separator {:#018x}, but found {:#018x}, inFile.eof()={}",
                              PacketSeparator, sep, inFile.eof());
                retval = 1;
                break;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
    }

    fmt::print("Left read loop, read {} DataPackets from {}, wrote to {}\n",
         totalDataPackets, inputFilename, outputFilename);

    return retval;
}
