#include "clock.h"

#include <chrono>

namespace nostromo {

std::uint32_t NowMs() {
    using namespace std::chrono;
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace nostromo
