#ifndef OBSERVER_H
#define OBSERVER_H

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <iomanip>
#include <sys/time.h>

#include "../pbbslib/get_time.h"
#ifdef USE_PCM_LIB
#include "cpucounters.h"
#endif

namespace gbbs {
// Used to track access annotations via a buffer
struct observer {
#ifdef USE_PCM_LIB
    using uint64 = pcm::uint64;
    using bytesRW = typename std::tuple<uint64, uint64>;
    pcm::SystemCounterState before_state;
    pcm::SystemCounterState after_state;
#endif
    pbbs::timer* t;
    double start_time;
    std::stringstream buffer;
    std::ofstream file;
    size_t MB = 1024 * 1024;

    observer(std::string _file) {
        file.open(_file);
#ifdef USE_PCM_LIB
        set_initial_state();
#endif
    }

void set_timer(pbbs::timer* _t) {
    t = _t;
    start_time = _t->last_time;
}

#ifdef USE_PCM_LIB
    auto get_pcm_state() { return pcm::getSystemCounterState(); }

    ///
    /// Useful for setting "starting point"
    ///
    void set_initial_state() { before_state = get_pcm_state(); }

    //
    // Get bytes read and written between two states
    //
    bytesRW get_bytes_rw() {
        after_state = get_pcm_state();

        auto bytes_read
            = pcm::getBytesReadFromMC(before_state, after_state);
        auto bytes_written
            = pcm::getBytesWrittenToMC(before_state, after_state);

#ifdef USE_PMM
        bytes_read    +=
            pcm::getBytesReadFromPMM(before_state, after_state);
        bytes_written +=
            pcm::getBytesWrittenToPMM(before_state, after_state);
#endif

        before_state = get_pcm_state();

        return {bytes_read, bytes_written};
    }
#endif

    void write(std::string value) {
        //auto t = clock();

#ifdef USE_PCM_LIB
        bytesRW bytes = get_bytes_rw();
#endif
        // Append timestamp in clock ticks
        buffer << value
#ifdef USE_PCM_LIB
               << ",BytesRead="
               << (std::get<0>(bytes) / MB)
               << ",BytesWritten="
               << (std::get<1>(bytes) / MB)
#endif
               << ",ClockTicks="
               << (t->get_time() - start_time)// / CLOCKS_PER_SEC
               << "\n"
               << std::endl;
    }

    void write_no_read(std::string value) {
        buffer << value
               << ",ClockTicks="
               << (t->get_time() - start_time)
               << "\n"
               << std::endl;
    }

    void save() {
        file << buffer.rdbuf();
        file.close();
    }
};

// Probably add julia function here (Best place???)

extern struct observer access_observer;

} // namespace gbbs

#endif