/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* Written by Nils Maier in 2014. */

#include "op.hpp"

#include <iomanip>
#include <iostream>
#include <climits>

#include <boost/program_options.hpp>

using util::ConsoleHandler;

static void __cdecl progress(winx_file_info *f, uint64_t *count)
{
  if (!(++*count % 13579)) {
    std::wcout << L"\r" << util::light << *count << util::clear <<
               L" items so far…";
  }
}

static void move_file(
  Operation &op, winx_file_info *f, const winx_volume_region *g)
{
  op.fe->pop(f);

  auto target = *g;

  IO_STATUS_BLOCK iosb;
  MOVEFILE_DESCRIPTOR mfd;
  memset(&mfd, 0, sizeof(mfd));

  auto startlcn = 0;
  auto numlcns = f->disp.clusters;
  while (auto cur = (ULONG)min(numlcns, MAXULONG32 - 10)) {
    NTSTATUS status;
    {
      auto file = zen::openFile(f);
      mfd.FileHandle = file.get();
      mfd.StartVcn.QuadPart = startlcn;
      mfd.NumVcns = cur;
      mfd.TargetLcn.QuadPart = target.lcn;
      if (op.opts.verbose) {
        std::wcout << L"Moving " << cur << L" segments (" <<
                   op.vol(cur) << L") to " << target.lcn <<
                   L"(" << op.vol(target.length) << L")" <<
                   std::endl;
      }
      status = ::NtFsControlFile(op.vol, nullptr, nullptr, 0, &iosb,
                                 FSCTL_MOVE_FILE, &mfd,
                                 sizeof(mfd), nullptr, 0);
      if (NT_SUCCESS(status)) {
        //::FlushFileBuffers(mfd.FileHandle);
        ::NtWaitForSingleObject(op.vol, FALSE, nullptr);
        status = iosb.Status;
      }
    }

    op.ge->push(f);
    winx_ftw_dump_file(f, nullptr, nullptr);

    if (NT_SUCCESS(status)) {
      op.ge->pop(f);
      op.fe->push(f);
      numlcns -= cur;
      target.lcn += cur;
      target.length -= cur;
      continue;
    }

    // No success
    if (status == STATUS_ALREADY_COMMITTED) {
      // Area vanished. File is still good.
      op.fe->push(const_cast<winx_file_info *>(f));
    }
    std::stringstream ss;
    ss << std::showbase;
    ss << "Failed to move file: " << std::hex << status << std::endl;
    LPSTR errorText = nullptr;
    HMODULE lib = ::LoadLibrary(L"NTDLL.dll");
    ::FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER |
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_FROM_HMODULE |
      FORMAT_MESSAGE_IGNORE_INSERTS,
      lib,
      status,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPSTR)&errorText,
      0,
      nullptr);
    if (errorText) {
      ss << errorText;
      ::LocalFree(errorText);
    }
    else {
      ss << "Unknown error";
    }
    if (lib) {
      ::FreeLibrary(lib);
    }
    auto serr = ss.str();
    throw std::exception(serr.c_str());
  }

  // Success
  op.moved++;
  op.movedLen += f->disp.clusters;
  op.replaced = true;
  op.last = f;
}


static bool move_set(
  Operation &op, zen::FileEnumeration::files_t &files, winx_volume_region &r)
{
  try {
    for (auto i = files.begin(), e = files.end(); i != e &&
         !ConsoleHandler::gTerminated; ++i) {
      auto f = *i;

      if (op.opts.verbose) {
        std::wcout << L"Found " << f->path << L"(" <<
                   std::fixed << f->disp.blockmap->lcn <<
                   L", " << op.vol(f->disp.clusters) << L", frag: " <<
                   std::fixed << f->disp.fragments << L")" << std::endl;
      }
      move_file(op, f, &r);
      r.lcn += f->disp.clusters;
      r.length -= f->disp.clusters;
    }
    if (!op.opts.verbose) {
      if (r.length) {
        std::wcout << util::yellow << L" partially";
      }
      else {
        std::wcout << util::green;
      }
      std::wcout << L" closed using " << files.size() << (files.size() > 1 ?
                 L" files." : L" file.") << util::clear << std::endl;
    }
    return true;
  }
  catch (const std::exception &ex) {
    std::wcerr << std::endl << util::red << util::to_wstring(ex.what()) <<
               util::clear << std::endl;
    return false;
  }
}

static void defrag(Operation &op)
{
  std::vector<winx_file_info *> fragmented;
  for (auto i = op.fe->begin(), e = op.fe->end(); i != e &&
       !ConsoleHandler::gTerminated; ++i) {
    if (i->second->disp.fragments > 1) {
      fragmented.push_back(i->second);
    }
  }

  auto remaining = fragmented.size();
  for (auto i = fragmented.begin(), e = fragmented.end(); i != e &&
       !ConsoleHandler::gTerminated; ++i) {
    util::title << L"Defragmenting… Remaining: " << remaining-- << L" files. " <<
                op.metrics() << std::flush;

    if (*i == op.last) {
      if (op.opts.verbose) {
        std::wcout << L"Skipping " << (*i)->path << std::endl;
      }
      op.fe->pop(*i);
    }
    if (op.opts.verbose) {
      std::wcout << L"Handling file at: " << (*i)->path << L" (" <<
                 op.vol((*i)->disp.clusters) << L", frags: " <<
                 std::fixed << (*i)->disp.fragments << L")" << std::endl;
    }
    else {
      std::wcout << L"\r" << util::light << (*i)->path + 4 << util::clear <<
                 L" frags: " << util::red << (*i)->disp.fragments << util::clear <<
                 L"…" << std::flush;
    }
    auto g = op.ge->best((*i)->disp.clusters);
    if (!g) {
      continue;
    }
    try {
      move_file(op, *i, g);
      if (!op.opts.verbose) {
        std::wcout << util::green << L" defragmented." << util::clear << std::endl;
      }
    }
    catch (const std::exception &ex) {
      std::wcerr << std::endl << (*i)->path << L": " << util::red <<
                 util::to_wstring(ex.what()) << util::clear << std::endl;
      op.ge->scan();
    }
  }
  std::wcout << std::endl;
}

static bool widen_behind(Operation &op, const winx_volume_region *g,
                         size_t maxMoves)
{
  auto r = *g;
  size_t moved = 0;
  uint64_t movedlen = 0;
  for (auto i = 0; op.opts.widen && !ConsoleHandler::gTerminated &&
       movedlen < op.opts.maxSize / 2 && moved < maxMoves ; ++i) {

    auto f = op.fe->findAt(r.lcn + r.length);
    if (!f) {
      break;
    }
    auto target = op.ge->best(f->disp.clusters, &r, true);
    if (!target && f->disp.clusters >= op.opts.maxSize &&
        r.length >= f->disp.clusters) {
      target = &r;
    }
    if (!target) {
      break;
    }
    try {
      move_file(op, f, target);
      if (target == &r) {
        r.lcn -= f->disp.clusters;
      }
      r.length += f->disp.clusters;
      movedlen += f->disp.clusters;
      moved++;
    }
    catch (const std::exception &ex) {
      std::wcerr << std::endl << f->path << ": " << util::red <<
                 util::to_wstring(ex.what()) << util::clear << std::endl;
      return false;
    }
  }
  if (moved > 0 && !op.opts.verbose) {
    std::wcout << util::blue << L" widened" << util::clear << L" to " <<
               op.vol(r.length) << L" by moving " << moved << L" files." << std::endl;
  }
  return moved > 0;
}

static void close_gaps(Operation &op)
{
  bool partialOK = false;
  for (auto g = op.ge->next(); g && !ConsoleHandler::gTerminated;
       g = op.ge->next()) {
    if (!op.opts.aggressive && g->length > op.opts.maxSize) {
      op.ge->pop(g);
      continue;
    }
    util::title << op.ge->count() << L" gaps remaining… " << op.metrics() <<
                std::flush;

    auto p = (double)g->lcn / op.vol.info.total_clusters * 100.0;
    std::wcout << L"\rGap: " << util::light << std::setw(8) <<
               std::right << op.vol(g->length) << util::clear <<
               L" @ " << util::light << std::setw(12) << g->lcn <<
               util::clear << L" (" << std::setw(5) <<
               std::right << std::setprecision(1) << std::fixed <<
               p << L"%) …" << std::flush;
    auto files = op.fe->findBest(g, partialOK);
    if (!files.empty()) {
      auto r = *g;
      if (!move_set(op, files, r)) {
        op.ge->scan();
        continue;
      }
      partialOK = false;
    }
    else {
      auto widened = widen_behind(op, g, partialOK ? 100 : 3);
      if (!widened && partialOK) {
        op.ge->pop(g);
        partialOK = false;
      }
      else {
        partialOK = true;
      }
    }
  }
}

void Options::parse(int argc, wchar_t **argv)
{
  namespace po = boost::program_options;
  po::options_description desc("Allowed options");
  desc.add_options()
  ("help,h",
   "Produce this help message")
  ("version,V",
   "Version information")
  ("volume",
   po::wvalue<std::wstring>(),
   "Volume to defrag")
  ("maxsize,m",
   po::value<size_t>(&maxSize)->
   default_value(102400),
   "Maximum gap size in KB to consider")
  ("verbose,v", "Set verbosity")
  ("widen,w", "Attempt to close more gaps by widening gaps first")
  ("aggressive,a",
   "Aggressive processing (disregarding maxsize)")
  ("no-gaps", "Do not attempt to close gaps")
  ("no-defrag", "Do not attempt to defrag files")
  ;
  po::positional_options_description p;
  p.add("volume", -1);
  po::variables_map vm;
  po::store(po::wcommand_line_parser(argc, argv).
            options(desc).positional(p).run(), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::stringstream ss;
    ss << desc;
    std::wcout << L"Usage: " << util::light << L"stopgap [options] <volume>" <<
               util::clear << std::endl << std::endl;
    std::wcout << ss.str().c_str() << std::endl << std::endl;
    std::wcout <<
               L"  * The number of gaps may initially increase, but will decrease towards the\n"
               L"    end of the operation. This is expected."
               << std::endl;
    std::wcout <<
               L"  * The sizes displayed are always rounded up to the nearest cluster size. This\n"
               L"    in particular means that less data might have been moved than indicated by\n"
               L"    this program."
               << std::endl;
    std::wcout <<
               L"  * For best fill use the <aggressive> and <widen> options. However, please note\n"
               L"    that this will potentially move around a lot more data and therefore put\n"
               L"    more strain on the disk."
               << std::endl;
    std::wcout <<
               L"  * It is sometimes claimed that defragmentation of solid state disks is\n"
               L"    unnecessary or worse. While it is true that defragmentation puts additional\n"
               L"    strain on the disk and that the raw access times do not really improve, one\n"
               L"    should keep in mind that there is still a file system layered on top of the\n"
               L"    actual disk, which might underperform when fragmentation reaches a certain\n"
               L"    point. (E.g. look up \"$ATTRIBUTE_LIST\")"
               << std::endl;
    throw Exit(1);
  }
  if (vm.count("version")) {
    util::Version v;
    std::wcout << util::light << v.product << util::green << L" v" << v.major <<
               "." << v.minor << util::clear << std::endl;
    std::wcerr << v.copyright << std::endl;
#if defined(__INTEL_COMPILER)
    auto maj = __INTEL_COMPILER / 100;
    auto min = __INTEL_COMPILER % 100;
    std::wcout << std::endl << L"Compiled with: "
               << util::light << L"Intel ICC " << maj << L"." << min
               << std::endl << util::clear;
#elif defined(_MSC_VER)
    auto maj = _MSC_VER / 100;
    auto min = _MSC_VER % 100;
    std::wcout << std::endl << L"Compiled with: "
               << util::light << L"Microsoft MSVC " << maj << L"." << min
               << std::endl << util::clear;
#endif
    std::wcout << std::endl;
    throw Exit(0);
  }
  if (vm.count("volume")) {
    auto v = vm["volume"].as<std::wstring>();
    volume = (char) v[0];
  }
  verbose = vm.count("verbose");
  aggressive = vm.count("aggressive") > 0;
  gaps = vm.count("no-gaps") < 1;
  defrag = vm.count("no-defrag") < 1;
  widen = vm.count("widen") > 0;

  if ((volume < 'a' || volume > 'z') && (volume < 'A' || volume > 'Z')) {
    throw std::exception("You need to specify a volume!");
  }
}

void Operation::init(int argc, wchar_t **argv)
{
  opts.parse(argc, argv);
  vol.init(opts.volume);
  opts.maxSize = opts.maxSize * 1024 / vol.info.bytes_per_cluster;
  std::wcout << std::setw(20) << std::left << L"Processing volume: " <<
             util::light << (wchar_t)toupper(opts.volume) << L": " << vol.info.label << " ("
             << vol.info.fs_name << L")" << util::clear << std::endl;
  std::wcout << std::setw(20) << std::left << L"Total size: " << util::light <<
             vol(vol.info.total_clusters) << util::clear << std::endl;
  std::wcout << std::setw(20) << std::left << L"Free size: " << util::light <<
             vol(vol.info.free_bytes / vol.info.bytes_per_cluster) <<
             util::clear << std::endl;
  std::wcout << std::setw(20) << std::left << L"Bytes per cluster: " <<
             util::light
             << vol.info.bytes_per_cluster << util::clear << std::endl;
  std::wcout << std::setw(20) << std::left << L"Using max gap size: " <<
             util::light << vol(opts.maxSize) << util::clear << std::endl;
  std::wcout << std::endl;

  util::title << L"Enumerating files…" << std::flush;

  ge.reset(new zen::GapEnumeration(opts.volume));
  uint64_t count = 0;
  fe.reset(new zen::FileEnumeration(opts.volume, (ftw_progress_callback)progress,
                                    &count));
  std::wcout << L"\rFound " << util::light << fe->count() << util::clear <<
             L" processable files in total" << std::endl;
  std::wcout << L"Found " << util::yellow << fe->unprocessable() << util::clear
             << L" unprocessable files" << std::endl;

  if (opts.verbose) {
    std::wcout << util::yellow;
    for (auto i = fe->unmovable().begin(), e = fe->unmovable().end(); i != e; ++i) {
      std::wcout << ((*i)->path + 4) << std::endl;
    }
    std::wcout << util::clear;
  }

  std::wcout << L"There are " << util::light << fe->fragmented() << util::clear
             << L" fragmented files" << std::endl;
  std::wcout << L"Initial gap count: " << util::light << ge->count() <<
             util::clear << std::endl;
  std::wcout << std::endl;
}

void Operation::run()
{
  LARGE_INTEGER li;
  ::QueryPerformanceCounter(&li);
  start = li.QuadPart;

  replaced = true;
  while (!ConsoleHandler::gTerminated && replaced) {

    if (opts.defrag) {
      // Some defragmentation.
      defrag(*this);
      ge->scan();
    }

    replaced = false;

    if (opts.gaps) {
      close_gaps(*this);
      ge->scan();
    }
  }

  util::title << L"Finishing…" << std::flush;
  ge->scan();
  std::wcout << std::endl << L"Final gap count: " << util::light << ge->count()
             << util::clear << std::endl;
  std::wcout << L"Carried out " << util::light << moved << util::clear <<
             L" successful moves, having moved " << util::light << vol(movedLen) <<
             util::clear << L" (" << vol(movedLen / seconds()) << L"/sec)." <<
             std::endl;

  uint64_t smallish = 0, smallsize = 0;
  uint64_t largish = 0, largesize = 0;
  auto largest = ge->sbegin();
  if (largest != ge->send()) {
    std::wcout << L"Largest consecutive gap: " << util::blue <<
               vol(largest->first) << util::clear << std::endl;
  }
  for (auto i = ge->begin(), e = ge->end(); i != e; ++i) {
    if (i->second->length <= opts.maxSize) {
      smallish++;
      smallsize += i->second->length;
    }
    else if (!opts.verbose) {
      largish++;
      largesize += i->second->length;
    }
    else {
      std::wcout << vol(i->second->length) << L" free bytes @ " <<
                 std::fixed << i->second->lcn << std::endl;
    }
  }
  if (largish) {
    std::wcout << util::green << largish << util::clear <<
               L" large gaps covering " << util::light << vol(largesize)
               << util::clear << std::endl;
  }
  if (smallish) {
    std::wcout << util::red << smallish << util::clear <<
               L" small gaps covering " << util::light << vol(smallsize)
               << util::clear << std::endl;
  }
}

std::wstring Operation::metrics() const
{
  auto s = seconds();
  std::wstringstream ss;
  ss.imbue(std::locale(""));
  ss << std::fixed << std::setprecision(2) << (moved / s) <<
     L" moves/sec, " << vol(movedLen / s) << L"/sec";
  return ss.str();
}