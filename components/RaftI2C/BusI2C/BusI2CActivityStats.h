/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BusI2CActivityStats.h
//
// Where the I2C worker task spends its time, in windows of about one second.
//
// The worker records the time spent in each part of a pass (application handler, scanning, queued requests,
// mux/power/IO expanders, polling). When a window ends its totals become the reported window, so a reader
// always sees one complete window. Recording is done on the worker task; reading (getJson) from any task.
//
// Time in bus transactions made directly by other tasks (outside the worker) is not included.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftArduino.h"
#include "RaftThreading.h"

class BusI2CActivityStats
{
public:
    enum Section
    {
        SECTION_HANDLER,
        SECTION_SCAN,
        SECTION_REQUESTS,
        SECTION_MUX_IO,
        SECTION_POLL,
        NUM_SECTIONS
    };

    BusI2CActivityStats()
    {
        RaftMutex_init(_lastMutex);
    }
    ~BusI2CActivityStats()
    {
        RaftMutex_destroy(_lastMutex);
    }

    /// @brief Add time spent in a section of the current pass
    void addSectionUs(Section section, uint64_t us)
    {
        if (section < NUM_SECTIONS)
            _cur.sectionUs[section] += us;
    }

    /// @brief End a worker pass, and end the window if it has lasted long enough
    /// @param passStartUs time the active part of the pass started
    /// @param passEndUs time the active part of the pass ended
    /// @param scanProbesTotal total scan probes so far (a running count)
    /// @param pollStepsTotal total poll steps so far (a running count)
    void endPass(uint64_t passStartUs, uint64_t passEndUs, uint32_t scanProbesTotal, uint32_t pollStepsTotal)
    {
        if (_windowStartUs == 0)
        {
            _windowStartUs = passStartUs;
            _windowStartProbes = scanProbesTotal;
            _windowStartPollSteps = pollStepsTotal;
        }
        uint64_t passUs = passEndUs - passStartUs;
        _cur.passes++;
        _cur.activeUs += passUs;
        if (passUs > _cur.maxPassUs)
            _cur.maxPassUs = passUs;

        // End of window
        if (passEndUs - _windowStartUs >= WINDOW_US)
        {
            _cur.windowUs = passEndUs - _windowStartUs;
            _cur.scanProbes = scanProbesTotal - _windowStartProbes;
            _cur.pollSteps = pollStepsTotal - _windowStartPollSteps;
            if (RaftMutex_lock(_lastMutex, 0))
            {
                _last = _cur;
                _lastValid = true;
                RaftMutex_unlock(_lastMutex);
            }
            _cur = Window();
            _windowStartUs = passEndUs;
            _windowStartProbes = scanProbesTotal;
            _windowStartPollSteps = pollStepsTotal;
        }
    }

    /// @brief Get the last complete window as JSON
    /// @return e.g. {"windowMs":1002,"passes":180,"activeUs":51234,"busyPct":5.1,"maxPassUs":4210,
    ///         "us":{"handler":0,"scan":20100,"requests":120,"muxIO":40,"poll":30974},"scanProbes":310,"pollSteps":30}
    String getJson() const
    {
        Window w;
        bool valid = false;
        if (RaftMutex_lock(_lastMutex, 10))
        {
            w = _last;
            valid = _lastValid;
            RaftMutex_unlock(_lastMutex);
        }
        if (!valid)
            return "{}";
        double busyPct = w.windowUs ? (w.activeUs * 100.0 / w.windowUs) : 0;
        char buf[300];
        snprintf(buf, sizeof(buf),
                 R"({"windowMs":%u,"passes":%u,"activeUs":%u,"busyPct":%.1f,"maxPassUs":%u,)"
                 R"("us":{"handler":%u,"scan":%u,"requests":%u,"muxIO":%u,"poll":%u},"scanProbes":%u,"pollSteps":%u})",
                 (unsigned)(w.windowUs / 1000), (unsigned)w.passes, (unsigned)w.activeUs, busyPct, (unsigned)w.maxPassUs,
                 (unsigned)w.sectionUs[SECTION_HANDLER], (unsigned)w.sectionUs[SECTION_SCAN],
                 (unsigned)w.sectionUs[SECTION_REQUESTS], (unsigned)w.sectionUs[SECTION_MUX_IO],
                 (unsigned)w.sectionUs[SECTION_POLL], (unsigned)w.scanProbes, (unsigned)w.pollSteps);
        return String(buf);
    }

private:
    static constexpr uint64_t WINDOW_US = 1000000;

    struct Window
    {
        uint64_t windowUs = 0;
        uint64_t activeUs = 0;
        uint64_t maxPassUs = 0;
        uint64_t sectionUs[NUM_SECTIONS] = {};
        uint32_t passes = 0;
        uint32_t scanProbes = 0;
        uint32_t pollSteps = 0;
    };

    // Current window (worker task only)
    Window _cur;
    uint64_t _windowStartUs = 0;
    uint32_t _windowStartProbes = 0;
    uint32_t _windowStartPollSteps = 0;

    // Last complete window (shared)
    mutable RaftMutex _lastMutex;
    Window _last;
    bool _lastValid = false;
};
