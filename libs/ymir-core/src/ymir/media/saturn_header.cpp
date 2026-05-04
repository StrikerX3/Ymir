#include <ymir/media/saturn_header.hpp>

#include <ymir/util/data_ops.hpp>

namespace ymir::media {

static std::string TrimWhitespace(std::string view) {
    auto start = view.find_first_not_of(" ");
    auto end = view.find_last_not_of(" ");

    if (start == std::string::npos && end == std::string::npos) {
        // The entire string is whitespace
        return "";
    }
    if (start == std::string::npos) {
        start = 0;
    }
    if (end == std::string::npos) {
        end = view.size();
    }
    return view.substr(start, end + 1);
}

static std::string ReadString(std::span<uint8, 256> data, std::size_t start, std::size_t end) {
    std::string view{(char *)&data[start], end - start + 1};
    std::transform(view.begin(), view.end(), view.begin(), [](char c) { return c == 0 ? ' ' : c; });
    return TrimWhitespace(view);
}

bool SaturnHeader::ReadFrom(std::span<uint8, 256> data) {
    hwID = ReadString(data, 0x00, 0x0F);
    if (!IsValid()) {
        return false;
    }

    makerID = ReadString(data, 0x10, 0x1F);
    productNumber = ReadString(data, 0x20, 0x29);
    version = ReadString(data, 0x2A, 0x2F);
    releaseDate = ReadString(data, 0x30, 0x37);
    deviceInfo = ReadString(data, 0x38, 0x3F);

    compatAreaCode = AreaCode::None;
    for (char code : ReadString(data, 0x40, 0x49)) {
        switch (code) {
        case 'A': compatAreaCode |= AreaCode::AsiaPAL; break;
        case 'B': compatAreaCode |= AreaCode::CentralSouthAmericaNTSC; break;
        case 'E': compatAreaCode |= AreaCode::EuropePAL; break;
        case 'J': compatAreaCode |= AreaCode::Japan; break;
        case 'K': compatAreaCode |= AreaCode::Korea; break;
        case 'L': compatAreaCode |= AreaCode::CentralSouthAmericaPAL; break;
        case 'T': compatAreaCode |= AreaCode::AsiaNTSC; break;
        case 'U': compatAreaCode |= AreaCode::NorthAmerica; break;
        }
    }

    compatPeripherals = PeripheralCode::None;
    for (char code : ReadString(data, 0x50, 0x5F)) {
        switch (code) {
        case 'A': compatPeripherals |= PeripheralCode::AnalogPad; break;
        case 'G': compatPeripherals |= PeripheralCode::VirtuaGun; break;
        case 'J': compatPeripherals |= PeripheralCode::ControlPad; break;
        case 'K': compatPeripherals |= PeripheralCode::Keyboard; break;
        case 'M': compatPeripherals |= PeripheralCode::Mouse; break;
        case 'S': compatPeripherals |= PeripheralCode::SteeringWheel; break;
        case 'T': compatPeripherals |= PeripheralCode::Multitap; break;
        }
    }

    gameTitle = ReadString(data, 0x60, 0xCF);

    ipSize = util::ReadBE<uint32>(&data[0xE0]);
    masterStackSize = util::ReadBE<uint32>(&data[0xE8]);
    slaveStackSize = util::ReadBE<uint32>(&data[0xEC]);
    firstReadAddress = util::ReadBE<uint32>(&data[0xF0]);
    firstReadSize = util::ReadBE<uint32>(&data[0xF4]);

    return true;
}

} // namespace ymir::media
