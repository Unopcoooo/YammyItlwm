// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// XNU treats '-' options as booleans even if followed by '=number'. Numeric
// options must use a bare name. Legacy dashed spelling never activates ROC.
template <typename Parse> inline uint32_t rocChannelOption(Parse parse) {
    int channel = 0;
    if (!parse("itlwm_awdl_roc_once", &channel, sizeof(channel))) return 0;
    return channel == 6 || channel == 44 || channel == 149 ? uint32_t(channel) : 0;
}
}
