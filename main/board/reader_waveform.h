/* SPDX-License-Identifier: Apache-2.0
 * 各板型选择自己的面板波形。/ Each board selects its own panel waveform.
 */
#pragma once
#if READER_METALIO && !SIM_HOST
#include "my_waveform.h"
#define READER_WAVEFORM MY_WAVEFORM
#else
#include "e0470_epaper_waveform.h"
#define READER_WAVEFORM E0470_WAVEFORM
#endif
