/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* Written by Nils Maier in 2014. */

#include "util.hpp"
#include "resource.h"

#include <algorithm>
#include <codecvt>
#include <iostream>

namespace
{
const wchar_t *kEnviroment =
  L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
const wchar_t *kPath = L"PATH";

static std::wstring getModuleDir()
{
  wchar_t file[MAX_PATH], dir[MAX_PATH], drive[5];
  memset(dir, 0, sizeof(wchar_t) * MAX_PATH);
  DWORD size = ::GetModuleFileName(nullptr, file, MAX_PATH);
  if (!size) {
    throw std::exception("Failed to get module path");
  }
  if (_wsplitpath_s(file, drive, 5, dir, MAX_PATH, nullptr, 0, nullptr, 0) ||
      !*dir) {
    throw std::exception("Failed to get dir name");
  }
  std::wstring rv(drive);
  rv.append(dir);
  rv.resize(rv.length() - 1);
  return rv;
}

static std::wstring tolower(const std::wstring &str)
{
  std::wstring rv(str);
  std::transform(rv.begin(), rv.end(), rv.begin(), ::towlower);
  return rv;
}

static std::wstring toupper(const std::wstring &str)
{
  std::wstring rv(str);
  std::transform(rv.begin(), rv.end(), rv.begin(), ::towupper);
  return rv;
}
}

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
  : window_(nullptr), iconSm_(nullptr), iconLg_(nullptr), oldIconSm_(nullptr),
    oldIconLg_(nullptr)
{
  window_ = ::GetConsoleWindow();
  if (!window_) {
    return;
  }
  iconLg_ = (HICON)::LoadImage(
              ::GetModuleHandle(nullptr),
              MAKEINTRESOURCE(IDI_ICON1),
              IMAGE_ICON,
              0, 0,
              LR_DEFAULTSIZE);
  auto cxsmicon = ::GetSystemMetrics(SM_CXSMICON);
  iconSm_ = (HICON)::LoadImage(
              ::GetModuleHandle(nullptr),
              MAKEINTRESOURCE(IDI_ICON1),
              IMAGE_ICON,
              cxsmicon, cxsmicon,
              0);

  if (iconSm_) {
    oldIconSm_ = (HICON)::SendMessage(window_, WM_SETICON, ICON_SMALL,
                                      (LPARAM)iconSm_);
  }
  if (iconLg_) {
    oldIconLg_ = (HICON)::SendMessage(window_, WM_SETICON, ICON_BIG,
                                      (LPARAM)iconLg_);
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
  if (iconSm_) {
    ::DestroyIcon(iconSm_);
  }
  if (iconLg_) {
    ::DestroyIcon(iconLg_);
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

static std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cvt;

std::wstring to_wstring(const std::string &str)
{
  return cvt.from_bytes(str);
}

std::string to_string(const std::wstring &wstr)
{
  return cvt.to_bytes(wstr);
}

const color_t clear(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
const color_t light(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE  |
                    FOREGROUND_INTENSITY);
const color_t green(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
const color_t blue(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
const color_t red(FOREGROUND_RED | FOREGROUND_INTENSITY);
const color_t yellow(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);

void registerPath()
{
  try {
    std::wcout << L"Registering Path" << std::endl;
    std::wstring path = getModuleDir();
    std::wcout << L"Add: " << path << std::endl;
    HKEY env = nullptr;
    if (::RegCreateKeyEx(
          HKEY_LOCAL_MACHINE,
          kEnviroment,
          0,
          nullptr,
          0,
          KEY_ALL_ACCESS,
          nullptr,
          &env,
          nullptr) != ERROR_SUCCESS) {
      throw std::exception("Failed to open or create environment key");
    }
    DWORD len = 1 << 20;
    DWORD type = REG_EXPAND_SZ;
    std::unique_ptr<wchar_t[]> val(new wchar_t[len]);
    LONG res = ::RegGetValue(env, nullptr, kPath,
                             RRF_RT_REG_SZ | RRF_RT_REG_BINARY |
                             RRF_RT_REG_EXPAND_SZ | RRF_RT_REG_MULTI_SZ |
                             RRF_NOEXPAND, &type,
                             val.get(), &len);
    if (res == ERROR_MORE_DATA) {
      throw std::exception("Buffer too small (unhandled)");
    }
    if (res != ERROR_SUCCESS) {
      if (::RegSetValueEx(env, kPath, 0, REG_EXPAND_SZ, (BYTE *)path.c_str(),
                          (path.length() + 1) * sizeof(wchar_t)) != ERROR_SUCCESS) {
        throw std::exception("Failed to create value");
      }
      return;
    }
    std::wcout << L"Current: " << val.get() << std::endl;
    std::wstring str(val.get());
    std::wstring upath(toupper(path)), ustr(toupper(str));
    if (ustr.find(upath) != std::wstring::npos) {
      throw std::exception("Already present in PATH");
    }
    if (!str.empty()) {
      str.append(L";");
    }
    str.append(path);
    std::wcout << L"Setting: " << str << std::endl;
    if (::RegSetValueEx(env, kPath, 0, REG_EXPAND_SZ, (BYTE *)str.c_str(),
                        (str.length() + 1) * sizeof(wchar_t)) != ERROR_SUCCESS) {
      throw std::exception("Failed to set value");
    }
  }
  catch (const std::exception &ex) {
    std::wcerr << to_wstring(ex.what()) << std::endl;
    return;
  }
}
void unregisterPath()
{
  try {
    std::wcout << "Unregistering Path" << std::endl;
    std::wstring path = getModuleDir();
    std::wcout << L"Remove: " << path << std::endl;
    HKEY env = nullptr;
    if (::RegCreateKeyEx(
          HKEY_LOCAL_MACHINE,
          kEnviroment,
          0,
          nullptr,
          0,
          KEY_ALL_ACCESS,
          nullptr,
          &env,
          nullptr) != ERROR_SUCCESS) {
      throw std::exception("Failed to open or create environment key");
    }
    DWORD len = 1 << 20;
    DWORD type = REG_EXPAND_SZ;
    std::unique_ptr<wchar_t[]> val(new wchar_t[len]);
    LONG res = ::RegGetValue(env, nullptr, kPath,
                             RRF_RT_REG_SZ | RRF_RT_REG_BINARY |
                             RRF_RT_REG_EXPAND_SZ | RRF_RT_REG_MULTI_SZ |
                             RRF_NOEXPAND, &type,
                             val.get(), &len);
    if (res == ERROR_MORE_DATA) {
      throw std::exception("Buffer too small (unhandled)");
    }
    if (res != ERROR_SUCCESS) {
      throw std::exception("No PATH key");
    }
    std::wcout << L"Current: " << val.get() << std::endl;
    path.append(L";");
    std::wstring str(val.get());
    std::wstring upath(toupper(path)), ustr(toupper(str));
    auto pos = std::wstring::npos;
    if ((pos = ustr.find(upath)) == std::wstring::npos) {
      path.resize(path.length() - 1);
      upath.resize(upath.length() - 1);
      path = L";" + path;
      upath = L";" + upath;
      if ((pos = ustr.find(upath)) == std::wstring::npos) {
        path = path.substr(1);
        upath = upath.substr(1);
        if ((pos = ustr.find(upath)) == std::wstring::npos) {
          throw std::exception("Not present in PATH");
        }
      }
    }
    str = str.replace(pos, path.length(), L"");
    std::wcout << L"Setting: " << str << std::endl;
    if (::RegSetValueEx(env, kPath, 0, REG_EXPAND_SZ, (BYTE *)str.c_str(),
                        (str.length() + 1) * sizeof(wchar_t)) != ERROR_SUCCESS) {
      throw std::exception("Failed to set value");
    }
  }
  catch (const std::exception &ex) {
    std::wcerr << to_wstring(ex.what()) << std::endl;
    return;
  }
}

}