/* $Id$ */

/** @file ai_event.cpp Implementation of AIEvent. */

#include "ai_event_types.hpp"

#include <queue>

struct AIEventData {
	std::queue<AIEvent *> stack;
};

/* static */ void AIEventController::CreateEventPointer()
{
	assert(AIObject::GetEventPointer() == NULL);

	AIObject::GetEventPointer() = new AIEventData();
}

/* static */ void AIEventController::FreeEventPointer()
{
	AIEventData *data = (AIEventData *)AIObject::GetEventPointer();

	/* Free all waiting events (if any) */
	while (!data->stack.empty()) {
		AIEvent *e = data->stack.front();
		data->stack.pop();
		e->Release();
	}

	/* Now kill our data pointer */
	delete data;
}

/* static */ bool AIEventController::IsEventWaiting()
{
	if (AIObject::GetEventPointer() == NULL) AIEventController::CreateEventPointer();
	AIEventData *data = (AIEventData *)AIObject::GetEventPointer();

	return !data->stack.empty();
}

/* static */ AIEvent *AIEventController::GetNextEvent()
{
	if (AIObject::GetEventPointer() == NULL) AIEventController::CreateEventPointer();
	AIEventData *data = (AIEventData *)AIObject::GetEventPointer();

	if (data->stack.empty()) return NULL;

	AIEvent *e = data->stack.front();
	data->stack.pop();
	return e;
}

/* static */ void AIEventController::InsertEvent(AIEvent *event)
{
	if (AIObject::GetEventPointer() == NULL) AIEventController::CreateEventPointer();
	AIEventData *data = (AIEventData *)AIObject::GetEventPointer();

	event->AddRef();
	data->stack.push(event);
}

