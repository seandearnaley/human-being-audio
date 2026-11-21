// TF_NONBLOCKING marks a function as real-time safe. With clang it expands to
// [[clang::nonblocking]], and tf_engine is built with -Werror=function-effects,
// so the compiler proves that nothing reachable from such a function allocates,
// locks, throws or makes a system call. Other compilers see nothing.
#pragma once

#if defined(__clang__) && defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define TF_NONBLOCKING [[clang::nonblocking]]
#endif
#endif

#ifndef TF_NONBLOCKING
#define TF_NONBLOCKING
#endif
