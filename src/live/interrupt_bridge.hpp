// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
#pragma once

#include <chrono>
#include <stop_token>
#include <thread>

#include "mux.hpp"

namespace parrot_to_klv {

// Bridge the interrupt flag to a stop_source for sink.close(token). The signal
// handler only sets an atomic flag (async-signal-safe), and a handler cannot
// call stop_source::request_stop() safely either, so a watcher thread polls the
// flag and forwards it. Without this, the final close() would still drain the
// video branch at wall-clock speed after the emission loop has already honored
// the interrupt — the drain tail this exists to cut short. Shared by file
// replay and live ingest, which have the same drain to cut.
class InterruptBridge {
  public:
    InterruptBridge() {
        watcher_ = std::jthread([src = source_](std::stop_token self) mutable {
            while (!self.stop_requested()) {
                if (is_mux_interrupted()) {
                    src.request_stop();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    std::stop_token token() const { return source_.get_token(); }

  private:
    std::stop_source source_;
    std::jthread watcher_;
};

}  // namespace parrot_to_klv
