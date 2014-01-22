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

  Options()
    : maxSize(102400), volume('\0'), verbose(0), aggressive(false) {
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
  const winx_file_info *last;
  bool replaced;

  Operation()
    : moved(0), movedLen(0), last(nullptr), replaced(false) {
  }
  void init(int argc, wchar_t **argv);
  void run();
};
