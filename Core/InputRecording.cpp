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

#include <algorithm>
#include <cstdlib>
#include <string>

#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/System/OSD.h"
#include "Core/AgentInput.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceRtc.h"
#include "Core/InputRecording.h"
#include "Core/Replay.h"

namespace InputRecording {

// How much of a recording a crash can lose.
static constexpr int FLUSH_INTERVAL_VBLANKS = 300;

static Path recordingPath;
// A reset ends a recording, and the next boot mustn't start it over.
static bool recordingDone = false;
static int vblanksSinceFlush = 0;
static bool playbackFinished = false;
// Set from the boot's kernel init until the game stops.
static bool booted = false;
static int32_t rtcBaseSeconds = 0;
static MarkerRanges captureMarkers;
static std::vector<int> captures;

bool ParseCaptureMode(std::string_view text, CaptureMode *mode) {
	if (text.empty() || text == "full") {
		*mode = CaptureMode::Full;
	} else if (text == "screenshots") {
		*mode = CaptureMode::Screenshots;
	} else if (text == "savestates") {
		*mode = CaptureMode::Savestates;
	} else {
		return false;
	}
	return true;
}

static bool ParsePositive(std::string_view text, int *value) {
	if (text.empty() || text.size() > 9) {
		return false;
	}
	int parsed = 0;
	for (char c : text) {
		if (c < '0' || c > '9') {
			return false;
		}
		parsed = parsed * 10 + (c - '0');
	}
	*value = parsed;
	return parsed > 0;
}

bool ParseMarkerSpec(std::string_view spec, MarkerRanges *ranges) {
	ranges->clear();
	if (spec.empty()) {
		return true;
	}
	// By hand, since SplitString drops empty items and "1," isn't a valid spec.
	for (size_t start = 0; start <= spec.size(); ) {
		const size_t comma = spec.find(',', start);
		const std::string_view item = spec.substr(start, comma == spec.npos ? spec.npos : comma - start);
		start = comma == spec.npos ? spec.size() + 1 : comma + 1;

		size_t dash = item.find('-');
		int first = 0;
		int last = 0;
		if (dash == item.npos) {
			if (!ParsePositive(item, &first)) {
				return false;
			}
			last = first;
		} else if (!ParsePositive(item.substr(0, dash), &first) || !ParsePositive(item.substr(dash + 1), &last) || last < first) {
			return false;
		}
		ranges->push_back({first, last});
	}

	std::sort(ranges->begin(), ranges->end());
	MarkerRanges merged;
	for (const auto &range : *ranges) {
		if (!merged.empty() && range.first <= merged.back().second + 1) {
			merged.back().second = std::max(merged.back().second, range.second);
		} else {
			merged.push_back(range);
		}
	}
	*ranges = merged;
	return true;
}

bool MarkerSelected(const MarkerRanges &ranges, int marker) {
	if (ranges.empty()) {
		return true;
	}
	for (const auto &range : ranges) {
		if (marker >= range.first && marker <= range.second) {
			return true;
		}
	}
	return false;
}

static void OnMarker(int marker) {
	if (!g_Config.launchCaptureDir.empty() && MarkerSelected(captureMarkers, marker)) {
		captures.push_back(marker);
	}
}

void OnKernelInit() {
	if (booted) {
		// The new kernel's RTC comes from the host clock.
		if (ReplayIsExecuting() || ReplayIsSaving()) {
			RtcSetBaseTime(rtcBaseSeconds, 0);
		}
		return;
	}
	booted = true;
	playbackFinished = false;
	vblanksSinceFlush = 0;
	captures.clear();

	if (!g_Config.launchInputRecording.empty()) {
		ParseMarkerSpec(g_Config.launchCaptureMarkers, &captureMarkers);
		ReplaySetMarkerHandler(&OnMarker);
		if (!ReplayExecuteFile(g_Config.launchInputRecording)) {
			ERROR_LOG(Log::System, "Can't replay the input recording '%s'", g_Config.launchInputRecording.c_str());
		}
	} else if (!g_Config.launchInputRecordingCreate.empty() && !recordingDone) {
		recordingPath = g_Config.launchInputRecordingCreate;
		ReplayBeginSave();
		// The file keeps whole seconds.
		RtcSetBaseTime(RtcBaseTime(), 0);
	}
	rtcBaseSeconds = RtcBaseTime();
}

static void FinishRecording() {
	ReplayMarkEnd(CoreTiming::GetGlobalTimeUs());
	if (!ReplayFlushFile(recordingPath)) {
		ERROR_LOG(Log::System, "Can't write the input recording '%s'", recordingPath.c_str());
	}
	ReplayAbort();
	recordingPath.clear();
	recordingDone = true;
}

static void FinishPlayback(const char *reason) {
	ReplayAbort();
	playbackFinished = true;
	AgentInput::AbortStep(reason);
	// A replay writes the stick into sceCtrl's state, and real input only moves it again on a change.
	__CtrlSetAnalogXY(CTRL_STICK_LEFT, 0.0f, 0.0f);
	__CtrlSetAnalogXY(CTRL_STICK_RIGHT, 0.0f, 0.0f);
}

void OnShutdown() {
	booted = false;
	AgentInput::AbortStep("The game stopped");
	if (!recordingPath.empty()) {
		FinishRecording();
	} else if (!g_Config.launchInputRecording.empty()) {
		ReplayAbort();
	}
}

void OnVblank() {
	if (!recordingPath.empty()) {
		if (++vblanksSinceFlush >= FLUSH_INTERVAL_VBLANKS) {
			vblanksSinceFlush = 0;
			ReplayFlushFile(recordingPath);
		}
		return;
	}
	if (g_Config.launchInputRecording.empty() || playbackFinished) {
		return;
	}
	if (!ReplayIsExecuting() || !ReplayHasMoreEvents()) {
		// Resuming hands the game back to real input.
		FinishPlayback("The replay ended");
		Core_Break(BreakReason::InputRecordingEnd);
	}
}

void OnStateLoaded() {
	if (!recordingPath.empty()) {
		FinishRecording();
		g_OSD.Show(OSDType::MESSAGE_WARNING, "A loaded state ended the input recording", 5.0f, "inputrecording");
	} else if (ReplayIsExecuting() && !g_Config.launchInputRecording.empty()) {
		FinishPlayback("A loaded state ended the replay");
		g_OSD.Show(OSDType::MESSAGE_WARNING, "A loaded state ended the replay", 5.0f, "inputrecording");
	}
}

bool PlaybackFinished() {
	return playbackFinished;
}

std::vector<int> TakeCaptures() {
	std::vector<int> taken;
	taken.swap(captures);
	return taken;
}

}  // namespace InputRecording
