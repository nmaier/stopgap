/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* Written by Nils Maier in 2014. */

#pragma once

#include <windows.h>
#include <memory>
#include <sstream>

#include <boost/config.hpp>

#if defined(BOOST_NO_CXX11_NULLPTR)
#define nullptr 0
#endif

namespace util
{

class ConsoleHandler
{
private:
  static BOOL WINAPI HandlerRoutine(DWORD ctrlType) {
    void(*handler)(int) = nullptr;
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      ::InterlockedExchange(&gTerminated, 1);
      return TRUE;
    default:
      return FALSE;
    }
  }

public:
  static volatile long gTerminated;

  ConsoleHandler() {
    ::SetConsoleCtrlHandler(HandlerRoutine, TRUE);
  }
  ~ConsoleHandler() {
    ::SetConsoleCtrlHandler(HandlerRoutine, FALSE);
  }
};

struct ConsoleIcon {
  HWND window_;
  HICON icon_;
  HICON oldIconSm_, oldIconLg_;

  ConsoleIcon();
  ~ConsoleIcon();
};

struct Version {
  DWORD major;
  DWORD minor;
  std::wstring product;
  std::wstring copyright;
  Version();
};

std::wstring to_wstring(const std::string &str);
std::string to_string(const std::wstring &wstr);

// Because imbue()ing a locale on wcout/wcerr does not work when _O_U16TEXT or
// any Unicode text mode for that matter.
class proxy : public std::wostream
{
private:
  class buf : public std::wstringbuf
  {
  private:
    std::wostream &os_;
  public:
    buf(std::wostream &os) : os_(os) {}
    ~buf() {
      pubsync();
    }
    int sync() {
      os_ << str();
      str(L"");
      return !os_;
    }
  };

public:
  proxy(std::wostream *os) : std::wostream(new buf(*os)) {
    imbue(std::locale(""));
    *this << std::showbase;
  }
  ~proxy() {
    delete rdbuf();
  }
};

// Because imbue()ing a locale on wcout/wcerr does not work when _O_U16TEXT or
// any Unicode text mode for that matter.
class Title : public std::wostream
{
private:
  class buf : public std::wstringbuf
  {
  private:
    //const std::wstring prefix;
    // prefix(L"StopGap — ")
  public:
    buf() {}
    ~buf() {
      pubsync();
    }
    virtual int sync() override;
  };

public:
  Title() : std::wostream(new buf()) {
    imbue(std::locale(""));
  }
  ~Title() {
    flush();
    delete rdbuf();
  }
};

extern Title title;

struct color_t {
  WORD color_;
  color_t(WORD color) : color_(color) {}
  inline std::wostream &operator<<(std::wostream &s) const {
    s.flush();
    ::SetConsoleTextAttribute(::GetStdHandle(STD_OUTPUT_HANDLE), color_);
    ::SetConsoleTextAttribute(::GetStdHandle(STD_ERROR_HANDLE), color_);
    return s;
  }
};

inline std::wostream &operator<<(std::wostream &s, const color_t &color)
{
  color << s;
  return s;
}

extern const color_t clear;
extern const color_t light;
extern const color_t green;
extern const color_t blue;
extern const color_t red;
extern const color_t yellow;


} // namespace win