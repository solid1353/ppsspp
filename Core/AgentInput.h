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

#include <string>

#include "Common/CommonTypes.h"

// Complete pad states installed from outside (the agent.* debugger events), for scripts that drive
// a game frame by frame. Its own lock guards it, so anything here can be called from any thread,
// except where noted.
namespace AgentInput {

struct State {
	u32 buttons = 0;
	u8 analog[2] = {128, 128};
};

// Replaces the input the game sees until released. Refused during a replay.
bool Set(const State &state, std::string *error);
void Release();
// The input of the last sample, and whether a state set here controlled it.
void Get(State *state, bool *controlled);

// Runs frames vblanks with state in place of what's there now, if state isn't null, then restores
// that. Returns the step's id, or 0 and an error. A step from a stopped CPU runs and stops again; the
// caller resumes the CPU. One from a running CPU keeps running.
int BeginStep(int frames, const State *state, std::string *error);

struct StepResult {
	bool done = false;
	bool failed = false;
	u32 startFrame = 0;
	u32 endFrame = 0;
	std::string error;
};
StepResult GetStepResult(int id);
void AbortStep(const char *reason);

// On the CPU thread.
void OnBoot();
void OnVblank();
// Inside the input sample, with sceCtrl's lock held: Apply before a replay or recording sees the
// input, NoteSample with what came out of that.
void Apply(u32 &buttons, u8 analog[2]);
void NoteSample(u32 buttons, const u8 analog[2]);

}  // namespace AgentInput
