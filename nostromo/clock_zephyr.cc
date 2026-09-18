#include "clock.h"

#include <zephyr/kernel.h>  // k_uptime_get_32

namespace nostromo {

std::uint32_t NowMs() {
    return k_uptime_get_32();
}

}  // namespace nostromo
