// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include <condition_variable>
#include <mutex>

struct StartGate {
    std::mutex mu;
    std::condition_variable cv;
    int ready = 0;
    bool start = false;

    void arrive_and_wait(int total) {
        std::unique_lock<std::mutex> lock(mu);
        ready++;
        if (ready == total) {
            cv.notify_all();
        } else {
            cv.wait(lock, [this, total] { return ready == total; });
        }
        cv.wait(lock, [this] { return start; });
    }

    void wait_for_ready(int total) {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [this, total] { return ready >= total; });
    }

    void release() {
        std::lock_guard<std::mutex> lock(mu);
        start = true;
        cv.notify_all();
    }
};
