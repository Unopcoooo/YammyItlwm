#include "../../itlwm/awdl/ActionFrame.hpp"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ItlAwdl::Summary summary;
    ItlAwdl::inspect(data, size, summary);
    return 0;
}
