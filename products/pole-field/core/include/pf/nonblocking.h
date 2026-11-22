// PF_NONBLOCKING marks a function as real-time safe. With clang it expands to
// [[clang::nonblocking]], and pf_engine is built with -Werror=function-effects,
// so the compiler proves that nothing reachable from Engine::process allocates,
// locks, throws or makes a system call.
#pragma once

#if defined(__clang__) && defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define PF_NONBLOCKING [[clang::nonblocking]]
#endif
#endif

#ifndef PF_NONBLOCKING
#define PF_NONBLOCKING
#endif
