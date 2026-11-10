/* Calls into original code that is not ported yet: PENDING_NOTE(0xADDR, "Name") logs once and the caller
   takes a neutral path. `grep -rn PENDING_NOTE src` is the to-do list; never a silent stub. Addresses
   are seg:off packed as 0xSSSSOOOO (the program at segment 1000h, exe.h). */
#pragma once
#include "platform.h"
#include <stdio.h>

#define PENDING_NOTE(addr, name)                                                                   \
    do {                                                                                           \
        static int pending_seen_;                                                                  \
        if (!pending_seen_) {                                                                      \
            char pending_m_[160];                                                                  \
            pending_seen_ = 1;                                                                     \
            snprintf(pending_m_, sizeof pending_m_, "pending: %s %04x:%04x not ported yet", (name), \
                     (unsigned)((uint32_t)(addr) >> 16), (unsigned)((uint32_t)(addr) & 0xffff));   \
            plat_log(pending_m_);                                                                  \
        }                                                                                          \
    } while (0)
