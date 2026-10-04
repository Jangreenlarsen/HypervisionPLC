// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
#pragma once
#include <string>
#include <vector>
#include "counter_config.h"
#include "timer_config.h"

extern std::string g_out;                 // alt CLI-output siden sidste reset
extern std::vector<std::string> g_calls;  // kaldte (stubbede) funktioner, raa symbolnavne
extern CounterConfig g_last_counter_cfg; extern int g_last_counter_id;
extern TimerConfig g_last_timer_cfg; extern int g_last_timer_id;
