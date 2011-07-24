// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/xidle.h"

#include <gdk/gdkx.h>
#include <inttypes.h>

#include "base/logging.h"
#include "power_manager/xidle_observer.h"

namespace power_manager {

static inline int64 XSyncValueToInt64(XSyncValue value) {
  return (static_cast<int64>(XSyncValueHigh32(value)) << 32 |
          static_cast<int64>(XSyncValueLow32(value)));
}

static inline void XSyncInt64ToValue(XSyncValue* xvalue, int64 value) {
  XSyncIntsToValue(xvalue, value, value >> 32);
}

XIdle::XIdle()
    : idle_counter_(0),
      min_timeout_(kint64max) {
}

XIdle::~XIdle() {
  ClearTimeouts();
}

bool XIdle::Init(XIdleObserver* observer) {
  CHECK(GDK_DISPLAY()) << "Display not initialized";
  int major_version, minor_version;
  if (XSyncQueryExtension(GDK_DISPLAY(), &event_base_, &error_base_) &&
      XSyncInitialize(GDK_DISPLAY(), &major_version, &minor_version)) {
    XSyncSystemCounter* counters;
    int ncounters;
    counters = XSyncListSystemCounters(GDK_DISPLAY(), &ncounters);

    if (counters) {
      for (int i = 0; i < ncounters; i++) {
        if (counters[i].name && strcmp(counters[i].name, "IDLETIME") == 0) {
          idle_counter_ = counters[i].counter;
          break;
        }
      }
      XSyncFreeSystemCounterList(counters);
      if (idle_counter_ && observer) {
        observer_ = observer;
        gdk_window_add_filter(NULL, GdkEventFilterThunk, this);
      }
    }
  }
  return idle_counter_ != 0;
}

bool XIdle::AddIdleTimeout(int64 idle_timeout_ms) {
  DCHECK_NE(idle_counter_, 0);
  DCHECK_GT(idle_timeout_ms, 1);

  if (idle_timeout_ms < min_timeout_) {
    min_timeout_ = idle_timeout_ms;

    // Setup an alarm to fire when the user was idle, but is now active.
    // This occurs when old_idle_time > min_timeout_ - 1, and the user becomes
    // active.
    XSyncAlarm alarm = CreateIdleAlarm(min_timeout_ - 1,
                                       XSyncNegativeTransition);
    if (!alarm)
      return false;
    if (!alarms_.empty()) {
      XSyncDestroyAlarm(GDK_DISPLAY(), alarms_.front());
      alarms_.pop_front();
    }
    alarms_.push_front(alarm);
  }

  // Send idle event when new_idle_time >= idle_timeout_ms
  XSyncAlarm alarm = CreateIdleAlarm(idle_timeout_ms, XSyncPositiveTransition);
  if (alarm)
    alarms_.push_back(alarm);
  return alarm != 0;
}

bool XIdle::GetIdleTime(int64* idle_time_ms) {
  DCHECK_NE(idle_counter_, 0);
  XSyncValue value;
  if (XSyncQueryCounter(GDK_DISPLAY(), idle_counter_, &value)) {
    *idle_time_ms = XSyncValueToInt64(value);
    return true;
  }
  return false;
}

bool XIdle::ClearTimeouts() {
  std::list<XSyncAlarm>::iterator it = alarms_.begin();
  for (; it != alarms_.end(); it++) {
    XSyncDestroyAlarm(GDK_DISPLAY(), *it);
  }
  alarms_.clear();
  min_timeout_ = kint64max;
  return true;
}

XSyncAlarm XIdle::CreateIdleAlarm(int64 idle_timeout_ms,
                                  XSyncTestType test_type) {
  uint64 mask = XSyncCACounter |
                XSyncCAValue |
                XSyncCATestType |
                XSyncCADelta;
  XSyncAlarmAttributes attr;
  attr.trigger.counter = idle_counter_;
  attr.trigger.test_type = test_type;
  XSyncInt64ToValue(&attr.trigger.wait_value, idle_timeout_ms);
  XSyncIntToValue(&attr.delta, 0);
  return XSyncCreateAlarm(GDK_DISPLAY(), mask, &attr);
}

GdkFilterReturn XIdle::GdkEventFilter(GdkXEvent* gxevent, GdkEvent*) {
  XEvent* xevent = static_cast<XEvent*>(gxevent);
  XSyncAlarmNotifyEvent* alarm_event =
      static_cast<XSyncAlarmNotifyEvent*>(gxevent);
  CHECK(idle_counter_);
  CHECK(!alarms_.empty());

  XSyncValue value;
  if (xevent->type == event_base_ + XSyncAlarmNotify &&
      alarm_event->state != XSyncAlarmDestroyed &&
      XSyncQueryCounter(GDK_DISPLAY(), idle_counter_, &value)) {
    bool is_idle = !XSyncValueLessThan(alarm_event->counter_value,
                                       alarm_event->alarm_value);
    bool is_idle2 = !XSyncValueLessThan(value,
                                        alarm_event->alarm_value);
    if (is_idle == is_idle2) {
      int64 idle_time_ms = XSyncValueToInt64(alarm_event->counter_value);
      observer_->OnIdleEvent(is_idle, idle_time_ms);
    } else {
      LOG(INFO) << "Filtering out stale event";
    }
  }

  return GDK_FILTER_CONTINUE;
}

}  // namespace power_manager
