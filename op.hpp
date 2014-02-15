/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* Written by Nils Maier in 2014. */

#pragma once

#include "util.hpp"
#include "zen.hpp"

struct Options {
  size_t maxSize;
  int verbose;
  char volume;
  bool aggressive;
  bool gaps;
  bool defrag;
  bool widen;

  Options()
    : maxSize(102400), volume('\0'), verbose(0), aggressive(false), gaps(true),
      defrag(true), widen(false) {
  }

  void parse(int argc, wchar_t **argv);
};

struct Operation {
  zen::Volume vol;
  std::unique_ptr<zen::GapEnumeration> ge;
  std::unique_ptr<zen::FileEnumeration> fe;
  Options opts;
  size_t moved;
  uint64_t movedLen;
  uint64_t start;
  uint64_t freq;
  const winx_file_info *last;
  bool replaced;

  Operation()
    : moved(0), movedLen(0), last(nullptr), replaced(false) {
    LARGE_INTEGER li;
    ::QueryPerformanceFrequency(&li);
    freq = li.QuadPart;
  }
  void init(int argc, wchar_t **argv);
  void run();
  double seconds() const {
    LARGE_INTEGER li;
    ::QueryPerformanceCounter(&li);
    return (li.QuadPart - start) / (double)freq;
  }
  std::wstring metrics() const;
};

class Exit : public std::exception
{
public:
  Exit(int code) : std::exception("Exiting program"), code_(code) {}

  const int code_;
};