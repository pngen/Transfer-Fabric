#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(TRANSFER_FABRIC_STATIC)
#    define TF_API
#  elif defined(TRANSFER_FABRIC_EXPORTS)
#    define TF_API __declspec(dllexport)
#  else
#    define TF_API __declspec(dllimport)
#  endif
#else
#  if defined(TRANSFER_FABRIC_EXPORTS)
#    define TF_API __attribute__((visibility("default")))
#  else
#    define TF_API
#  endif
#endif

#ifndef TF_ENABLE_CUDA
#  if defined(TF_CUDA_ENABLED)
#    define TF_ENABLE_CUDA 1
#  else
#    define TF_ENABLE_CUDA 0
#  endif
#endif
#ifndef TF_PLATFORM_WINDOWS
#  if defined(_WIN32)
#    define TF_PLATFORM_WINDOWS 1
#  else
#    define TF_PLATFORM_WINDOWS 0
#  endif
#endif
#ifndef TF_PLATFORM_POSIX
#  if !defined(_WIN32)
#    define TF_PLATFORM_POSIX 1
#  else
#    define TF_PLATFORM_POSIX 0
#  endif
#endif
