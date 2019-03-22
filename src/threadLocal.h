//
//  threadLocal.h
//  Efficient Compression Tool
//

#pragma once

// From https://stackoverflow.com/questions/18298280/how-to-declare-a-variable-as-thread-local-portably
#ifndef NOMULTI
# ifndef thread_local
#  if __STDC_VERSION__ >= 201112 && !defined __STDC_NO_THREADS__
#   define thread_local _Thread_local
#  elif defined _WIN32 && ( \
        defined _MSC_VER || \
        defined __ICL || \
        defined __DMC__ || \
        defined __BORLANDC__ )
#   define thread_local __declspec(thread)
/* note that ICC (linux) and Clang are covered by __GNUC__ */
#  elif defined __GNUC__ || \
        defined __SUNPRO_C || \
        defined __xlC__
#   define thread_local __thread
#  else
#   error "Cannot define thread_local"
#  endif
# endif
#else
#define thread_local
#endif
