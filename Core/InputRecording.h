// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <string_view>
#include <utility>
#include <vector>

// A recording of a whole boot's input, made and replayed through Core/Replay.h from power-on
// (--input-recording-create and --input-recording), with screenshots and savestates captured at its
// markers (--input-recording-capture-*). See docs/input-recording.md.
namespace InputRecording {

enum class CaptureMode {
	Full,
	Screenshots,
	Savestates,
};

// 1-based marker numbers, as ascending ranges that don't touch.
typedef std::vector<std::pair<int, int>> MarkerRanges;

bool ParseCaptureMode(std::string_view text, CaptureMode *mode);
// A comma-separated list of positive numbers and ascending first-last ranges, such as 1,3-5.
// Overlapping and adjacent ranges merge. Empty selects every marker, as an empty list.
bool ParseMarkerSpec(std::string_view spec, MarkerRanges *ranges);
bool MarkerSelected(const MarkerRanges &ranges, int marker);

// After the kernel's init, which sets the RTC a replay overrides. That's every boot, and every
// sceKernelLoadExec, which a recording spans.
void OnKernelInit();
void OnShutdown();
void OnVblank();
// A loaded state moves the emulated clock, which a recording's timeline can't follow, so this ends
// whatever is running. Before the state is applied.
void OnStateLoaded();

// Whether the replay this run asked for has reached its end, or failed to start.
bool PlaybackFinished();

// The markers reached since the last call that are selected for capture.
std::vector<int> TakeCaptures();

}  // namespace InputRecording
