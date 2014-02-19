/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* Written by Nils Maier in 2014. */

#pragma once

#include "util.hpp"

#include <shlwapi.h>

#include <cstdint>
#include <map>
#include <vector>

#include <boost/pool/pool_alloc.hpp>
#include <boost/iterator/iterator_adaptor.hpp>

extern "C" {
#include "ntndk.h"
#include "zenwinx.h"

#ifdef I
#undef I
#endif
#ifdef E
#undef E
#endif
#ifdef D
#undef D
#endif
}

namespace zen
{
template<typename T>
inline void winx_freep(T **p)
{
  if (p && *p) {
    winx_free(*p);
    *p = nullptr;
  }
}

class winx
{
public:
  winx();
  ~winx();
};

class Volume
{
private:
  WINX_FILE *file_;

public:
  winx_volume_information info;

  Volume() : file_(nullptr) {}

  void init(char volume) {
    file_ = winx_vopen(volume);
    if (!file_) {
      throw std::exception("Failed to open volume");
    }
    if (winx_get_volume_information(volume, &info) < 0) {
      throw std::exception("Failed to query volume");
    }
  }

  ~Volume() {
    if (file_) {
      winx_fclose(file_);
      file_ = nullptr;
    }
  }

  HANDLE handle() {
    return file_->hFile;
  }

  operator HANDLE() {
    return file_->hFile;
  }

  std::wstring operator()(uint64_t clusters) const {
    static wchar_t buf[11];
    return StrFormatByteSizeW(clusters * info.bytes_per_cluster, buf, 11);
  }
};

namespace
{
struct file_close {
  void operator()(HANDLE h) {
    winx_defrag_fclose(h);
  }
};
} // namespace

typedef std::unique_ptr <void, file_close> File;

File openFile(const winx_file_info *file)
{
  HANDLE rv = nullptr;
  if (winx_defrag_fopen(const_cast<winx_file_info *>(file), WINX_OPEN_FOR_MOVE,
                        &rv)) {
    std::string ex("Failed to open file: ");
    ex.append(util::to_string(file->path));
    throw std::exception(ex.c_str());
  }
  return File(rv);
}

template<typename T>
class List
{
public:
  typedef T value_t;
  typedef value_t *ptr_t;

  template<typename U>
  class itr_t : public boost::iterator_adaptor
    <itr_t<U>, U, boost::use_default, boost::forward_traversal_tag>
  {
  public:
    itr_t()
      : itr_t::iterator_adaptor_(nullptr), head_(nullptr) {}

    explicit itr_t(U p, U head)
      : itr_t::iterator_adaptor_(p), head_(head) {}

  private:
    friend class boost::iterator_core_access;
    U head_;
    void increment() {
      if (head_) {
        auto next = base()->next;
        if (next == head_) {
          base_reference() = nullptr;
        }
        else {
          base_reference() = next;
        }
      }
    }
  };
  typedef itr_t<ptr_t> iterator;
  typedef itr_t<const ptr_t> const_iterator;

private:
  ptr_t head_;

public:
  explicit List(ptr_t head) : head_(head) {}

  iterator begin() const {
    return iterator(head_, head_);
  }
  iterator end() const {
    return iterator(nullptr, head_);
  }

  const_iterator cbegin() const {
    return const_iterator(head_);
  }
  const_iterator cend() const {
    return const_iterator(head_ ? head_->prev : nullptr);
  }
};

class GapEnumeration
{
private:
  typedef std::pair<uint64_t, winx_volume_region *> pair_t;
  typedef boost::fast_pool_allocator
  < pair_t, boost::default_user_allocator_new_delete, boost::details::pool::null_mutex, 1024 >
  alloc_t;
  typedef std::map<uint64_t, winx_volume_region *, std::less<uint64_t>, alloc_t>
  regions_t;
  typedef std::multimap<uint64_t, winx_volume_region *, std::less<uint64_t>, alloc_t>
  sizes_t;

  winx_volume_region *info_;
  regions_t regions_;
  sizes_t sizes_;
  const char volume_;

  void free() {
    if (info_) {
      winx_release_free_volume_regions(info_);
      info_ = nullptr;
    }
    regions_.clear();
    sizes_.clear();
  }

public:
  typedef const regions_t::value_type value_type;
  typedef regions_t::const_iterator const_iterator;
  typedef sizes_t::const_reverse_iterator size_iterator;

  GapEnumeration(char volume)
    : info_(nullptr), volume_(volume) {
    scan();
  }
  ~GapEnumeration() {
    free();
  }

  void filter();

  void scan() {
    free();
    info_ = winx_get_free_volume_regions(
              volume_,
              0,
              nullptr,
              this);
    filter();
  }

  const winx_volume_region *next() const {
    if (regions_.empty()) {
      return nullptr;
    }
    return regions_.begin()->second;
  }

  const winx_volume_region *best(
    uint64_t clusters,
    const winx_volume_region *not = nullptr,
    bool behindOnly = false) const;

  void pop(const winx_volume_region *r) {
    pop(r->lcn, r->length);
  }
  void pop(const uint64_t lcn, const uint64_t length);
  void pop(const winx_file_info *f);

  void push(const winx_file_info *f);

  const_iterator begin() const {
    return regions_.begin();
  }
  const_iterator end() const {
    return regions_.end();
  }

  regions_t::size_type count() const {
    return regions_.size();
  }

  size_iterator sbegin() const {
    return sizes_.rbegin();
  }
  size_iterator send() const {
    return sizes_.rend();
  }
};

class FileEnumeration
{
public:
  typedef std::vector<winx_file_info *> files_t;

private:
  typedef std::pair<uint64_t, winx_file_info *> pair_t;
  typedef boost::fast_pool_allocator
  < pair_t, boost::default_user_allocator_new_delete, boost::details::pool::null_mutex, 25600 >
  alloc_t;
  typedef std::multimap<uint64_t, winx_file_info *, std::less<uint64_t>, alloc_t>
  buckets_t;
  typedef std::map<uint64_t, winx_file_info *, std::less<uint64_t>, alloc_t>
  lcns_t;

  typedef boost::fast_pool_allocator
  < pair_t, boost::default_user_allocator_new_delete, boost::details::pool::null_mutex, 1024 >
  alloc_known_t;
  typedef std::multimap<buckets_t::key_type, buckets_t::mapped_type, std::greater<buckets_t::key_type>, alloc_known_t >
  known_t;

  static int terminator(void *) {
    return util::ConsoleHandler::gTerminated;
  }

  static known_t known_;

  const char volume_;
  buckets_t buckets_;
  lcns_t lcns_;
  files_t unmovable_;
  winx_file_info *info_;
  uint64_t fragmented_;
  uint64_t unprocessable_;

  void order(winx_file_info &f);

  void scan(ftw_progress_callback cb, void *userdata);

  void free() {
    if (info_) {
#ifndef NO_PATCH
      winx_scan_disk_release(info_);
#endif
      info_ = nullptr;
    }

    buckets_.clear();
    lcns_.clear();
    unmovable_.clear();
  }

public:
  typedef const buckets_t::value_type value_type;
  typedef buckets_t::iterator iterator;

  FileEnumeration(char volume, ftw_progress_callback cb = nullptr,
                  void *ud = nullptr)
    : volume_(volume), info_(nullptr), fragmented_(0), unprocessable_(0) {
    scan(cb, ud);
  }
  ~FileEnumeration() {
    free();
  }

  files_t findBest(uint64_t lcn, uint64_t length, bool partialOK);

  files_t findBest(const winx_volume_region *r, bool partialOK) {
    const auto lcn = r->lcn;
    auto len = r->length;
    return findBest(lcn, len, partialOK);
  }

  winx_file_info *findAt(uint64_t lcn);

  void pop(const winx_file_info *f);

  void push(winx_file_info *f);

  iterator begin()  {
    return buckets_.begin();
  }
  iterator end() {
    return buckets_.end();
  }

  buckets_t::size_type count() const {
    return buckets_.size();
  }
  uint64_t fragmented() const {
    return fragmented_;
  }
  uint64_t unprocessable() const {
    return unprocessable_;
  }

  const files_t &unmovable() const {
    return unmovable_;
  }
};

} // namespace zen