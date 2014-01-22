/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* Written by Nils Maier in 2014. */

#include "util.hpp"
#include "resource.h"

namespace util
{

int Title::buf::sync()
{
  static const std::wstring prefix(L"StopGap — ");

  auto s = prefix + str();
  ::SetConsoleTitle(s.c_str());
  str(L"");
  return 0;
}

Title title;

volatile long ConsoleHandler::gTerminated = 0;

ConsoleIcon::ConsoleIcon()
  : window_(nullptr), icon_(nullptr), oldIconSm_(nullptr),
    oldIconLg_(nullptr)
{
  window_ = ::GetConsoleWindow();
  if (!window_) {
    return;
  }
  icon_ = (HICON)::LoadIcon(::GetModuleHandle(nullptr),
                            MAKEINTRESOURCE(IDI_ICON1));
  if (icon_) {
    oldIconSm_ = (HICON)::SendMessage(window_, WM_SETICON, ICON_SMALL,
                                      (LPARAM)icon_);
    oldIconLg_ = (HICON)::SendMessage(window_, WM_SETICON, ICON_BIG,
                                      (LPARAM)icon_);
  }
}

ConsoleIcon::~ConsoleIcon()
{
  if (!window_) {
    return;
  }
  if (oldIconSm_) {
    ::SendMessage(window_, WM_SETICON, ICON_SMALL,
                  (LPARAM)oldIconSm_);
  }
  if (oldIconLg_) {
    ::SendMessage(window_, WM_SETICON, ICON_BIG,
                  (LPARAM)oldIconLg_);
  }
  if (icon_) {
    ::DestroyIcon(icon_);
  }
  window_ = nullptr;
}

Version::Version()
  : major(0), minor(0)
{
  wchar_t file[MAX_PATH];
  std::unique_ptr<char[]> ver;
  DWORD size = ::GetModuleFileName(nullptr, file, MAX_PATH);
  if (!size) {
    return;
  }
  DWORD handle;
  size = ::GetFileVersionInfoSizeEx(FILE_VER_GET_NEUTRAL, file, &handle);
  if (!size) {
    return;
  }
  ver.reset(new char[size]);
  if (!::GetFileVersionInfoEx(FILE_VER_GET_NEUTRAL, file, handle, size,
                              ver.get())) {
    return;
  }

  VS_FIXEDFILEINFO *ptr;
  UINT fsize;

  if (!::VerQueryValue(ver.get(), L"\\", (void **)&ptr, &fsize)) {
    return;
  }

  if (fsize != sizeof(VS_FIXEDFILEINFO)) {
    return;
  }

  major = (ptr->dwProductVersionMS & 0xffff0000) >> 16;
  minor = ptr->dwProductVersionMS & 0xffff;

  struct {
    WORD lang;
    WORD cp;
  } *trans;

  if (!::VerQueryValue(ver.get(), L"\\VarFileInfo\\Translation",
                       (LPVOID *)&trans, &fsize)) {
    return;
  }

  // Read the file description for each language and code page.

  for (auto i = 0; i < (fsize / sizeof(*trans)); ++i) {
    wchar_t sub[100];
    LPVOID val;
    UINT ssize;
    swprintf(sub, 100, L"\\StringFileInfo\\%04x%04x\\ProductName",
             trans[i].lang, trans[i].cp);
    if (!::VerQueryValue(ver.get(), sub, &val, &ssize) || !ssize) {
      continue;
    }
    product.assign((wchar_t *)val);

    swprintf(sub, 100, L"\\StringFileInfo\\%04x%04x\\LegalCopyRight",
             trans[i].lang, trans[i].cp);
    if (!::VerQueryValue(ver.get(), sub, &val, &ssize) || !ssize) {
      continue;
    }
    copyright.assign((wchar_t *)val);
    break;
  }
}

const color_t clear(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
const color_t light(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE  |
                    FOREGROUND_INTENSITY);
const color_t green(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
const color_t blue(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
const color_t red(FOREGROUND_RED | FOREGROUND_INTENSITY);
const color_t yellow(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
}