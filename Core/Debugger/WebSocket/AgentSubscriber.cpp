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
#include <cstring>
#include <string>
#include <vector>

#include "Common/Data/Format/JSONWriter.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Core/AgentInput.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/WebSocket/AgentSubscriber.h"
#include "Core/Debugger/WebSocket/InputSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/InputRecording.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Replay.h"
#include "Core/System.h"

struct WebSocketAgentState : public DebuggerSubscriber {
	// Input a disconnected client set would otherwise stay on for good.
	~WebSocketAgentState() override;

	void InputSet(DebuggerRequest &req);
	void InputGet(DebuggerRequest &req);
	void InputRelease(DebuggerRequest &req);
	void Step(DebuggerRequest &req);
	void ReplayStatus(DebuggerRequest &req);
	void ReplayStep(DebuggerRequest &req);
	void Shutdown(DebuggerRequest &req);

	void Broadcast(net::WebSocketServer *ws) override;

protected:
	struct PendingStep {
		int id;
		std::string event;
		std::string ticket;
	};

	void StartStep(DebuggerRequest &req, const char *event, int frames, const AgentInput::State *state);

	std::vector<PendingStep> pendingSteps_;
	bool controlling_ = false;
};

DebuggerSubscriber *WebSocketAgentInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketAgentState();
	map["agent.input.set"] = [p](DebuggerRequest &req) { p->InputSet(req); };
	map["agent.input.get"] = [p](DebuggerRequest &req) { p->InputGet(req); };
	map["agent.input.release"] = [p](DebuggerRequest &req) { p->InputRelease(req); };
	map["agent.step"] = [p](DebuggerRequest &req) { p->Step(req); };
	map["agent.replay.status"] = [p](DebuggerRequest &req) { p->ReplayStatus(req); };
	map["agent.replay.step"] = [p](DebuggerRequest &req) { p->ReplayStep(req); };
	map["agent.shutdown"] = [p](DebuggerRequest &req) { p->Shutdown(req); };
	return p;
}

WebSocketAgentState::~WebSocketAgentState() {
	if (!pendingSteps_.empty()) {
		AgentInput::AbortStep("The client disconnected");
	}
	if (controlling_) {
		AgentInput::Release();
	}
}

// Reads a complete pad state: 'buttons', an array of names as in input.buttons.send, and optional
// 'x' and 'y', -1.0 to 1.0. What's not named is released or centered.
static bool ParseState(DebuggerRequest &req, const JsonNode *node, AgentInput::State *state) {
	if (node->value.getTag() != JSON_OBJECT) {
		req.Fail("Invalid 'state' parameter type");
		return false;
	}
	double x = 0.0;
	double y = 0.0;
	for (const JsonNode *field : node->value) {
		if (!strcmp(field->key, "buttons")) {
			if (field->value.getTag() != JSON_ARRAY) {
				req.Fail("Invalid 'buttons' parameter type");
				return false;
			}
			for (const JsonNode *button : field->value) {
				if (button->value.getTag() != JSON_STRING) {
					req.Fail("Invalid 'buttons' entry type");
					return false;
				}
				auto info = WebSocketInputButtonLookup().find(button->value.toString());
				if (info == WebSocketInputButtonLookup().end()) {
					req.Fail(StringFromFormat("Unsupported button '%s'", button->value.toString()));
					return false;
				}
				state->buttons |= info->second;
			}
		} else if (!strcmp(field->key, "x") || !strcmp(field->key, "y")) {
			if (field->value.getTag() != JSON_NUMBER || field->value.toNumber() < -1.0 || field->value.toNumber() > 1.0) {
				req.Fail(StringFromFormat("Parameter '%s' must be between -1.0 and 1.0", field->key));
				return false;
			}
			(field->key[0] == 'x' ? x : y) = field->value.toNumber();
		} else {
			req.Fail(StringFromFormat("Unsupported 'state' key '%s'", field->key));
			return false;
		}
	}
	state->analog[0] = (u8)std::clamp((int)(x * 127.5 + 128.0), 0, 255);
	state->analog[1] = (u8)std::clamp((int)(-y * 127.5 + 128.0), 0, 255);
	return true;
}

// Replace the pad's whole input until released (agent.input.set)
//
// Parameters:
//  - state: object with 'buttons', an array of names as in input.buttons.send, and optional 'x' and
//    'y', -1.0 to 1.0. Whatever isn't named is released or centered.
//
// Refused during a replay. While recording, the recording stores this input.
//
// Response (same event name) with no extra data.
void WebSocketAgentState::InputSet(DebuggerRequest &req) {
	const JsonNode *node = req.data.get("state");
	if (!node) {
		return req.Fail("Missing 'state' parameter");
	}
	AgentInput::State state;
	if (!ParseState(req, node, &state)) {
		return;
	}
	std::string error;
	if (!AgentInput::Set(state, &error)) {
		return req.Fail(error);
	}
	controlling_ = true;
	req.Respond();
}

// Report the input of the last sample (agent.input.get)
//
// No parameters.
//
// Response (same event name):
//  - controlled: whether agent input set it.
//  - buttons: number, the PSP button mask.
//  - x, y: numbers, 0 to 255, centered at 128.
void WebSocketAgentState::InputGet(DebuggerRequest &req) {
	AgentInput::State state;
	bool controlled = false;
	AgentInput::Get(&state, &controlled);
	JsonWriter &json = req.Respond();
	json.writeBool("controlled", controlled);
	json.writeUint("buttons", state.buttons);
	json.writeInt("x", state.analog[0]);
	json.writeInt("y", state.analog[1]);
}

// Hand the pad back (agent.input.release)
//
// No parameters.
//
// Response (same event name) with no extra data.
void WebSocketAgentState::InputRelease(DebuggerRequest &req) {
	AgentInput::Release();
	controlling_ = false;
	req.Respond();
}

void WebSocketAgentState::StartStep(DebuggerRequest &req, const char *event, int frames, const AgentInput::State *state) {
	if (!currentDebugMIPS->isAlive() || coreState == CORE_POWERDOWN) {
		return req.Fail("CPU not started");
	}
	std::string error;
	const bool stopped = Core_IsStepping();
	const int id = AgentInput::BeginStep(frames, state, &error);
	if (id == 0) {
		return req.Fail(error);
	}
	const JsonNode *ticket = req.data.get("ticket");
	pendingSteps_.push_back(PendingStep{id, event, ticket ? json_stringify(ticket) : ""});
	if (stopped) {
		// As cpu.resume does it.
		Core_RunOnCPUThread([&] {
			if (currentMIPS->inDelaySlot) {
				Core_RequestCPUStep(CPUStepType::Into);
			}
			Core_Resume();
		});
	}
}

// Run some frames, then answer (agent.step)
//
// Parameters:
//  - frames: positive number of vblanks.
//  - state: optional, the input to hold for the step (see agent.input.set). What was set before
//    comes back afterwards.
//
// A stopped CPU stops again after the last frame. A running one keeps running.
//
// Response (same event name), once the step is done, or an error if it can't finish:
//  - startFrame, endFrame: vblank counts since boot, a half-open interval.
void WebSocketAgentState::Step(DebuggerRequest &req) {
	uint32_t frames = 0;
	if (!req.ParamU32("frames", &frames)) {
		return;
	}
	const JsonNode *node = req.data.get("state");
	AgentInput::State state;
	if (node && !ParseState(req, node, &state)) {
		return;
	}
	StartStep(req, "agent.step", (int)frames, node ? &state : nullptr);
}

// Report on a replay (agent.replay.status)
//
// No parameters.
//
// Response (same event name):
//  - executing: whether a replay is running.
//  - finished: whether the replay asked for on the command line has ended.
//  - us: emulated microseconds since boot.
//  - endUs: when the replay ends, in the same clock.
void WebSocketAgentState::ReplayStatus(DebuggerRequest &req) {
	bool executing = false;
	u64 us = 0;
	u64 endUs = 0;
	Core_RunOnCPUThread([&] {
		executing = ReplayIsExecuting();
		us = CoreTiming::GetGlobalTimeUs();
		endUs = ReplayLastEventTime();
	});
	JsonWriter &json = req.Respond();
	json.writeBool("executing", executing);
	json.writeBool("finished", InputRecording::PlaybackFinished());
	json.writeFloat("us", (double)us);
	json.writeFloat("endUs", (double)endUs);
}

// Run a stopped replay some frames further, then answer (agent.replay.step)
//
// Parameters:
//  - vblanks: positive number of vblanks.
//
// Response (same event name), once done, as agent.step. An error if the replay ends first.
void WebSocketAgentState::ReplayStep(DebuggerRequest &req) {
	uint32_t vblanks = 0;
	if (!req.ParamU32("vblanks", &vblanks)) {
		return;
	}
	bool executing = false;
	Core_RunOnCPUThread([&] {
		executing = ReplayIsExecuting();
	});
	if (!executing || !Core_IsStepping()) {
		return req.Fail("agent.replay.step needs a stopped replay");
	}
	StartStep(req, "agent.replay.step", (int)vblanks, nullptr);
}

// Close the emulator (agent.shutdown)
//
// No parameters.
//
// Response (same event name) with no extra data, without waiting for it to finish.
void WebSocketAgentState::Shutdown(DebuggerRequest &req) {
	req.Respond();
	System_ExitApp();
	Core_Stop();
}

void WebSocketAgentState::Broadcast(net::WebSocketServer *ws) {
	for (size_t i = 0; i < pendingSteps_.size(); ) {
		const PendingStep &pending = pendingSteps_[i];
		const AgentInput::StepResult result = AgentInput::GetStepResult(pending.id);
		if (!result.done) {
			i++;
			continue;
		}

		JsonWriter j;
		j.begin();
		if (result.failed) {
			j.writeString("event", "error");
			j.writeString("message", result.error);
			j.writeInt("level", (int)LogLevel::LERROR);
		} else {
			j.writeString("event", pending.event);
			j.writeUint("startFrame", result.startFrame);
			j.writeUint("endFrame", result.endFrame);
		}
		if (!pending.ticket.empty()) {
			j.writeRaw("ticket", pending.ticket);
		}
		j.end();
		ws->Send(j.str());
		pendingSteps_.erase(pendingSteps_.begin() + i);
	}
}
