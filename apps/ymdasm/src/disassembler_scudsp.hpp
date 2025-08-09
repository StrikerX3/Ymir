#pragma once

#include "disassembler.hpp"

#include <vector>

bool DisassembleSCUDSP(Disassembler &disasm, std::string_view origin, const std::vector<std::string> &args,
                       const std::string &inputFile);
