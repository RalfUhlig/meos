// Linux port: stand-in for the MSVC header <process.h>.
// Thread creation (_beginthread) is platform code and is ported to std::thread
// in the files that use it; nothing is declared here.
#pragma once

#include "../msvc_compat.h"
