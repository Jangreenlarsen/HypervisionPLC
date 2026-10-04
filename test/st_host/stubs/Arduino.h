// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
// Stub for Arduino.h — kun til PC-testen af ST-compiler/VM (BUG-433).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
uint32_t millis(void);
uint32_t micros(void);
static inline void delay(uint32_t) {}
static inline void yield(void) {}
#define IRAM_ATTR
#define RTC_NOINIT_ATTR
