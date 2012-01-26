/* Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_COMPILER_H_
#define VERITY_INCLUDE_LINUX_COMPILER_H_

#define unlikely(x) (x)

#define __force

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#endif  /* VERITY_INCLUDE_LINUX_COMPILER_H_ */
