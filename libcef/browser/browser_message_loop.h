// Copyright (c) 2012 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_BROWSER_MESSAGE_LOOP_H_
#define CEF_LIBCEF_BROWSER_BROWSER_MESSAGE_LOOP_H_
#pragma once

#include <stdint.h>

void InitExternalMessagePumpFactoryForUI();
void CefScheduleExternalMessagePumpWork(int64_t delay_ms);

#endif  // CEF_LIBCEF_BROWSER_BROWSER_MESSAGE_LOOP_H_
