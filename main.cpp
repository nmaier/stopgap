/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* Written by Nils Maier in 2014. */

#define BOOST_DISABLE_THREADS 1

#include "util.hpp"
#include "op.hpp"

#include <iostream>

#include <fcntl.h>
#include <io.h>

int wmain(int argc, wchar_t **argv)
{
  // Any RAII stuff here is leaked on purpose!
  // If any RAII stuff needs to be actually destructed, move it into a block inside the
  // main try-catch block.

  // Set up console.
  _setmode(_fileno(stdout), _O_U16TEXT);
  _setmode(_fileno(stderr), _O_U16TEXT);
  std::wcout = util::proxy();
  std::wcerr = util::proxy();

  std::wcout << util::clear;

  // Set up CTRL-C handler
  util::ConsoleHandler ch;

  zen::winx zw;

  Operation op;
  try {
    util::ConsoleIcon icon;

    op.init(argc, argv);
    op.run();
  }
  catch (const Exit &ex) {
    std::wcout << util::clear;
    _exit(ex.code_);
  }
  catch (const std::exception &ex) {
    std::wcerr << std::endl << L"Failed to process: " << util::red <<
               util::to_wstring(ex.what()) << util::clear << std::endl;
    InterlockedExchange(&util::ConsoleHandler::gTerminated, 3);
  }

  std::wcout << util::clear;

  // Call exit, to avoid all the deallocation stuff!
  _exit(util::ConsoleHandler::gTerminated);
}