// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <glib.h>

#include "shill/shill_event.h"

namespace shill {

EventQueueItem::EventQueueItem(EventDispatcher *dispatcher)
  : dispatcher_(dispatcher) {
  printf("Registering myself (%p)\n", this);
  dispatcher->RegisterCallbackQueue(this);
}

EventQueueItem::~EventQueueItem() {
  dispatcher_->UnregisterCallbackQueue(this);
}

void EventQueueItem::AlertDispatcher() {
  dispatcher_->ExecuteOnIdle();
}

void EventDispatcher::DispatchEvents() {
  for (size_t idx = 0; idx < queue_list_.size(); ++idx)
    queue_list_[idx]->Dispatch();
}

static gboolean DispatchEventsHandler(gpointer data) {
  EventDispatcher *dispatcher = static_cast<EventDispatcher *>(data);
  dispatcher->DispatchEvents();
  return false;
}

void EventDispatcher::ExecuteOnIdle() {
  g_idle_add(&DispatchEventsHandler, this);
}

void EventDispatcher::RegisterCallbackQueue(EventQueueItem *queue) {
  queue_list_.push_back(queue);
}

void EventDispatcher::UnregisterCallbackQueue(EventQueueItem *queue) {
  for (size_t idx = 0; idx < queue_list_.size(); ++idx) {
    if (queue_list_[idx] == queue) {
      queue_list_.erase(queue_list_.begin() + idx);
      return;
    }
  }
}

gboolean cb_func(gpointer data) {
  static int i = 0;
  EventDispatcherTest *dispatcher_test =
    static_cast<EventDispatcherTest *>(data);
  dispatcher_test->TimerFunction(i++);
  return true;
}

EventDispatcherTest::EventDispatcherTest(EventDispatcher *dispatcher) {
  int_callback_ = new ClassCallback<EventDispatcherTest, int>(this,
      &EventDispatcherTest::HandleInt);
  int_callback_queue_ = new EventQueue<int>(dispatcher);
  int_callback_queue_->AddCallback(int_callback_);
  g_timeout_add_seconds(1, cb_func, this);
}

EventDispatcherTest::~EventDispatcherTest() {
  int_callback_queue_->RemoveCallback(int_callback_);
  delete(int_callback_);
}

void EventDispatcherTest::TimerFunction(int counter) {
  printf("Callback func called %p\n", int_callback_queue_);
  if (counter > 3)
    int_callback_queue_->AddEvent(counter);
}

void EventDispatcherTest::HandleInt(int arg) {
  printf("Manager handling int %s %d\n", __func__, arg);
}


}  // namespace shill
