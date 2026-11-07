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

#include <cstring>
#include <mutex>

#include "Core/AgentInput.h"
#include "Core/Core.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Replay.h"

namespace AgentInput {

static std::mutex lock;
static bool hasOverride = false;
static State overrideState;
static bool hasStepState = false;
static State stepState;
// What the last sample gave the game.
static State lastState;
static bool lastControlled = false;
// Counts vblanks since boot.
static u32 frame = 0;

static int stepFramesLeft = 0;
static bool stepStops = false;
static int stepId = 0;
static int lastStepId = 0;
static StepResult lastResult;

static void CenterStick() {
	__CtrlSetAnalogXY(CTRL_STICK_LEFT, 0.0f, 0.0f);
}

bool Set(const State &state, std::string *error) {
	std::lock_guard<std::mutex> guard(lock);
	if (ReplayIsExecuting()) {
		*error = "Agent input is refused during a replay";
		return false;
	}
	overrideState = state;
	hasOverride = true;
	return true;
}

void Release() {
	{
		std::lock_guard<std::mutex> guard(lock);
		if (!hasOverride && !hasStepState) {
			return;
		}
		hasOverride = false;
		hasStepState = false;
	}
	CenterStick();
}

void Get(State *state, bool *controlled) {
	std::lock_guard<std::mutex> guard(lock);
	*state = lastState;
	*controlled = lastControlled;
}

int BeginStep(int frames, const State *state, std::string *error) {
	const bool stopped = Core_IsStepping();
	std::lock_guard<std::mutex> guard(lock);
	if (frames <= 0) {
		*error = "A step needs a positive frame count";
		return 0;
	}
	if (state && ReplayIsExecuting()) {
		*error = "Agent input is refused during a replay";
		return 0;
	}
	if (stepFramesLeft > 0) {
		*error = "Another step is in progress";
		return 0;
	}
	hasStepState = state != nullptr;
	if (state) {
		stepState = *state;
	}
	stepFramesLeft = frames;
	stepStops = stopped;
	stepId = ++lastStepId;
	lastResult = StepResult();
	lastResult.startFrame = frame;
	return stepId;
}

StepResult GetStepResult(int id) {
	std::lock_guard<std::mutex> guard(lock);
	if (id != stepId) {
		StepResult gone;
		gone.done = true;
		gone.failed = true;
		gone.error = "The step was replaced";
		return gone;
	}
	return lastResult;
}

void AbortStep(const char *reason) {
	bool centerStick = false;
	{
		std::lock_guard<std::mutex> guard(lock);
		if (stepFramesLeft <= 0) {
			return;
		}
		stepFramesLeft = 0;
		lastResult.done = true;
		lastResult.failed = true;
		lastResult.error = reason;
		centerStick = hasStepState && !hasOverride;
		hasStepState = false;
	}
	if (centerStick) {
		CenterStick();
	}
}

void OnBoot() {
	AbortStep("The game was reset");
	Release();
	std::lock_guard<std::mutex> guard(lock);
	frame = 0;
	lastState = State();
	lastControlled = false;
}

void OnVblank() {
	bool stop = false;
	bool centerStick = false;
	{
		std::lock_guard<std::mutex> guard(lock);
		frame++;
		if (stepFramesLeft > 0 && --stepFramesLeft == 0) {
			lastResult.endFrame = frame;
			lastResult.done = true;
			centerStick = hasStepState && !hasOverride;
			hasStepState = false;
			stop = stepStops;
		}
	}
	if (centerStick) {
		CenterStick();
	}
	if (stop) {
		Core_Break(BreakReason::AgentStep);
	}
}

void Apply(u32 &buttons, u8 analog[2]) {
	std::lock_guard<std::mutex> guard(lock);
	lastControlled = hasStepState || hasOverride;
	if (lastControlled) {
		const State &state = hasStepState ? stepState : overrideState;
		buttons = state.buttons;
		memcpy(analog, state.analog, sizeof(state.analog));
	}
}

void NoteSample(u32 buttons, const u8 analog[2]) {
	std::lock_guard<std::mutex> guard(lock);
	lastState.buttons = buttons;
	memcpy(lastState.analog, analog, sizeof(lastState.analog));
}

}  // namespace AgentInput
