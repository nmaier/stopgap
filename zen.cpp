/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* Written by Nils Maier in 2014. */

#include "zen.hpp"

#include <boost/dynamic_bitset.hpp>
#include <boost/multi_array.hpp>
#include <boost/regex.hpp>

static const boost::wregex excluded(L":\\$|\\\\\\$|"
                                    L"\\\\(?:safeboot\\.fs$|Gobackio\\.bin$|PGPWDE|bootwiz|BootAuth.\\.sys|\\$dcsys\\$|bootstat\\.dat|bootsqm\\.dat)|"
                                    L":\\\\(?:io\\.sys|msdos\\.sys|ibmbio\\.com|ibmdos\\.com|drbios\\.sys|System Volume Information)",
                                    boost::regex_constants::normal | boost::regex_constants::icase |
                                    boost::regex_constants::optimize | boost::regex_constants::nosubs);

static const size_t maxlen = 256;


namespace
{

inline bool minlcn(const winx_blockmap &a, const winx_blockmap &b)
{
  return a.lcn < b.lcn;
}

}

namespace zen
{

winx::winx()
{
  winx_init_library();
}

winx::~winx()
{
  winx_unload_library();
}


const winx_volume_region *GapEnumeration::best(
  uint64_t clusters,  const winx_volume_region *not,  bool behindOnly) const
{
  if (sizes_.empty()) {
    return nullptr;
  }
  // Find a matching region.
  auto range = sizes_.equal_range(clusters);
  for (auto i = range.first; i != range.second; ++i) {
    if (behindOnly && i->second->lcn <= not->lcn) {
      continue;
    }
    if (i->second != not) {
      return i->second;
    }
  }

  // No matching region. Find first greater region.
  // We don't just want a region that is only somewhat latter, as this
  // would cause additional small gaps, we might not be able to fill later.
  for (auto i = sizes_.upper_bound(max(clusters * 3 / 2, clusters + 512)),
       e = sizes_.end(); i != e; ++i) {
    if (behindOnly && i->second->lcn <= not->lcn) {
      continue;
    }
    if (i->second != not) {
      return i->second;
    }
  }
  // Still no region. Just return the max. region.
  for (auto i = sizes_.rbegin(), e = sizes_.rend(); i != e; ++i) {
    if (i->first < clusters) {
      break;
    }
    if (behindOnly && i->second->lcn <= not->lcn) {
      continue;
    }
    if (i->second != not) {
      return i->second;
    }
  }
  // Nothing :(
  return nullptr;
}

void GapEnumeration::filter()
{
  regions_.clear();
  sizes_.clear();
  auto regs = List<winx_volume_region>(info_);
  for (auto i = regs.begin(), e = regs.end(); i != e; ++i) {
    regions_.insert(regions_.end(), regions_t::value_type(i->lcn, &(*i)));
    sizes_.insert(sizes_.end(), sizes_t::value_type(i->length, &(*i)));
  }
}

void GapEnumeration::pop(const uint64_t lcn, const uint64_t length)
{
  // The idea here is that we always move files to the beginning of a gap.
  auto g = regions_.find(lcn);
  if (g == regions_.end()) {
#ifdef _DEBUG
    ::DebugBreak();
#endif
    scan();
    return;
  }
  auto range = sizes_.equal_range(g->second->length);
  for (auto i = range.first; i != range.second; ++i) {
    if (i->second != g->second) {
      continue;
    }
    sizes_.erase(i);
    break;
  }
  if (g->second->length > length) {
    auto n = g->second;
    regions_.erase(g);
    n->lcn += length;
    n->length -= length;
    regions_.insert(sizes_t::value_type(n->lcn, n));
    sizes_.insert(sizes_t::value_type(n->length, n));
    return;
  }
  if (g->second->length < length) {
    ::DebugBreak(); // Something went horribly wrong!
    return;
  }
  regions_.erase(g);
}

void GapEnumeration::pop(const winx_file_info *f)
{
  auto bm = List<winx_blockmap>(f->disp.blockmap);
  for (auto i = bm.begin(), e = bm.end(); i != e; ++i) {
    pop(i->lcn, i->length);
  }
}

void GapEnumeration::push(const winx_file_info *f)
{
  auto bm = List<winx_blockmap>(f->disp.blockmap);
  for (auto b = bm.begin(), be = bm.end(); b != be; ++b) {
    if (!b->length) {
      continue;
    }
    auto prev = std::prev(regions_.lower_bound(b->lcn));
    auto next = regions_.find(b->lcn + b->length);
    bool mergePrev = prev != regions_.end() &&
                     prev->second->lcn + prev->second->length == b->lcn;
    bool mergeNext = next != regions_.end();

    // Try to merge with existing region(s).
    if (mergePrev && mergeNext) {
      auto range = sizes_.equal_range(prev->second->length);
      for (auto i = range.first; i != range.second; ++i) {
        if (i->second != prev->second) {
          continue;
        }
        sizes_.erase(i);
        break;
      }
      range = sizes_.equal_range(next->second->length);
      for (auto i = range.first; i != range.second; ++i) {
        if (i->second != next->second) {
          continue;
        }
        sizes_.erase(i);
        break;
      }
      prev->second->length += b->length + next->second->length;
      regions_.erase(next);
      continue;
    }

    if (mergePrev) {
      auto range = sizes_.equal_range(prev->second->length);
      for (auto i = range.first; i != range.second; ++i) {
        if (i->second != prev->second) {
          continue;
        }
        sizes_.erase(i);
        break;
      }
      prev->second->length += b->length;
      sizes_.insert(sizes_t::value_type(prev->second->length, prev->second));
      continue;
    }
    if (mergeNext) {
      auto range = sizes_.equal_range(next->second->length);
      for (auto i = range.first; i != range.second; ++i) {
        if (i->second != next->second) {
          continue;
        }
        sizes_.erase(i);
        break;
      }
      auto n = next->second;
      regions_.erase(next);
      n->lcn = b->lcn;
      n->length += b->length;
      regions_.insert(regions_t::value_type(n->lcn, n));
      sizes_.insert(sizes_t::value_type(n->length, n));
      continue;
    }

    if (!info_) {
      DebugBreak();
      scan();
      return;
    }

    // Insert a new region.
    auto item = prev != regions_.end() ? prev->second : std::prev(
                  regions_.end())->second;
    auto nr = (winx_volume_region *)winx_list_insert((list_entry **)(
                void *)&info_, (list_entry *)item, sizeof(winx_volume_region));
    nr->lcn = b->lcn;
    nr->length = b->length;
    regions_.insert(regions_t::value_type(nr->lcn, nr));
    sizes_.insert(sizes_t::value_type(nr->length, nr));
  }
}

FileEnumeration::known_t FileEnumeration::known_;

void FileEnumeration::order(winx_file_info &f)
{
  if (!f.disp.blockmap || f.disp.blockmap == f.disp.blockmap->next) {
    return;
  }
  auto bm = List<winx_blockmap>(f.disp.blockmap);
  f.disp.blockmap = &*std::min_element(bm.begin(), bm.end(), minlcn);
}

void FileEnumeration::scan(ftw_progress_callback cb, void *userdata)
{
  free();
  info_ = winx_scan_disk(
            volume_,
            WINX_FTW_RECURSIVE | WINX_FTW_SKIP_RESIDENT_STREAMS | WINX_FTW_DUMP_FILES,
            nullptr,
            cb,
            terminator,
            userdata);
  if (!info_) {
    if (util::ConsoleHandler::gTerminated) {
      return;
    }
    throw std::exception("Failed to gather volume information");
  }
  List<winx_file_info> infos(info_);
  std::for_each(infos.begin(), infos.end(), [this](winx_file_info & f) {
    winx_freep(&f.name);
    if (f.disp.fragments > 1) {
      fragmented_++;
    }
    if (!f.disp.blockmap) {
      unprocessable_++;
      return;
    }
    if (boost::regex_search(f.path, excluded)) {
      unprocessable_++;
      return;
    }
    order(f);
    buckets_.insert(std::make_pair(f.disp.clusters, &f));
  });
}

winx_file_info *FileEnumeration::findAt(uint64_t lcn)
{
  if (lcns_.empty()) {
    for (auto bi = buckets_.begin(), be = buckets_.end(); bi != be; ++bi) {
      auto bm = zen::List<winx_blockmap>(bi->second->disp.blockmap);
      std::for_each(
        bm.begin(),
        bm.end(),
      [&](const winx_blockmap & block) {
        lcns_.insert(std::make_pair(block.lcn, bi->second));
      });
    }
  }
  auto m = lcns_.find(lcn);
  if (m == lcns_.end()) {
    return nullptr;
  }
  return m->second;
}

void FileEnumeration::pop(const winx_file_info *f)
{
  if (!lcns_.empty()) {
    auto bm = zen::List<winx_blockmap>(f->disp.blockmap);
    std::for_each(
      bm.begin(),
      bm.end(),
    [&](const winx_blockmap & i) {
      lcns_.erase(i.lcn);
    });
  }
  auto range = buckets_.equal_range(f->disp.clusters);
  for (auto i = range.first; i != range.second; ++i) {
    if (i->second == f) {
      buckets_.erase(i);
      return;
    }
  }
  assert(false);
}

void FileEnumeration::push(winx_file_info *f)
{
  if (!lcns_.empty()) {
    auto bm = zen::List<winx_blockmap>(f->disp.blockmap);
    std::for_each(
      bm.begin(),
      bm.end(),
    [this, f](const winx_blockmap & i) {
      lcns_.insert(std::make_pair(i.lcn, f));
    });
  }
  order(*f);
  buckets_.insert(std::make_pair(f->disp.clusters, f));
}

FileEnumeration::files_t FileEnumeration::findBest(uint64_t lcn,
    uint64_t length, bool partialOK)
{
  files_t rvs;

  auto filter = [&](const winx_file_info * f) -> bool {
    if (f->disp.blockmap->lcn <= lcn) {
      return true;
    }
    return false;
  };

  // Find perfect item
  {
    winx_file_info *perfect = nullptr;
    auto range = buckets_.equal_range(length);
    for (auto i = range.first; i != range.second; ++i) {
      if (filter(i->second)) {
        continue;
      }
      if (!perfect || perfect->disp.blockmap->lcn < i->second->disp.blockmap->lcn) {
        perfect = i->second;
      }
    }
    if (perfect) {
      rvs.push_back(perfect);
      return rvs;
    }
  }

  // Build candidate list
  files_t cands;
  {
    auto filter2 = [&](winx_file_info * f) -> bool {
      if (filter(f)) {
        return true;
      }
      if (length > maxlen) {
        if (length >= f->disp.clusters) {
          rvs.push_back(f);
          length -= f->disp.clusters;
        }
        return true;
      }
      return false;
    };
    known_.clear();
    for (auto i = buckets_t::reverse_iterator(buckets_.upper_bound(length)),
         e = buckets_.rend(); i != e; ++i) {
      if (!length) {
        // Already fulfilled.
        return rvs;
      }
      if (i->first > length) {
        continue;
      }
      if (filter2(i->second)) {
        continue;
      }
      auto k = known_.count(i->first);
      if ((i->first < 4 && k < 1) || k < 4) {
        known_.insert(*i);
      }
      auto range = known_.equal_range(i->first);
      for (auto ri = range.first; ri != range.second; ++ri) {
        if (ri->second->disp.blockmap->lcn >= i->second->disp.blockmap->lcn) {
          continue;
        }
        known_.erase(ri);
        known_.insert(*i);
        break;
      }
    }
    if (!length) {
      // Already fulfilled.
      return rvs;
    }
    for (auto i = known_.begin(), e = known_.end(); i != e; ++i) {
      cands.push_back(i->second);
    }
  }

  const size_t ncands = cands.size();
  if (!ncands) {
    // Cannot fulfill.
    if (!partialOK) {
      rvs.clear();
    }
    return rvs;
  }

  if (ncands == 1) {
    if (cands[0]->disp.clusters == length) {
      rvs.push_back(cands[0]);
    }
    else if (partialOK) {
      if (cands[0]->disp.clusters <= length) {
        rvs.push_back(cands[0]);
      }
    }
    else {
      rvs.clear();
    }
    return rvs;
  }

  const uint64_t nlength = length;

  typedef boost::dynamic_bitset<uint64_t> bitset;
  struct sol_t {
    uint64_t value;
    bitset items;
    bool operator==(const sol_t &rhs) const {
      return value == rhs.value && items.count() == rhs.items.count();
    }
    bool operator <(const sol_t &rhs) const {
      if (value == rhs.value) {
        return items.count() > rhs.items.count();
      }
      return value < rhs.value;
    }
    void add(size_t index) {
      if (items.size() <= index) {
        items.resize(index + 1);
      }
      items.set(index);
    }
  };
  typedef boost::multi_array<sol_t, 2> solutions_t;
  auto extends = boost::extents[ncands + 1][nlength + 1];
  solutions_t solutions(extends);

  for (uint64_t i = 1; i <= ncands; i++) {
    auto cand = cands[i - 1];
    auto candw = cand->disp.clusters;
    for (uint64_t w = 1; w <= nlength; w++) {
      if (candw <= w) {
        auto candsol = solutions[i - 1][w - candw];
        candsol.value += candw;
        candsol.add(i - 1);
        if (solutions[i - 1][w] < candsol) {
          solutions[i][w] = candsol;
        }
        else {
          solutions[i][w] = solutions[i - 1][w];
        }
      }
      else {
        solutions[i][w] = solutions[i - 1][w];
      }
    }
  }
  auto &solution = solutions[ncands][nlength];
  if (!partialOK && solution.value != nlength) {
    rvs.clear();
    return rvs;
  }
  for (auto i = solution.items.find_first(); i != bitset::npos;
       i = solution.items.find_next(i)) {
    rvs.push_back(cands[i]);
  }
  return rvs;
}

} // namespace zen