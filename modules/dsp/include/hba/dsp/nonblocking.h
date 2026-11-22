// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#if __has_cpp_attribute(clang::nonblocking)
#define HBA_NONBLOCKING [[clang::nonblocking]]
#else
#define HBA_NONBLOCKING
#endif
