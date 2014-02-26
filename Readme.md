StopGap
-------

Windows/NTFS free space defragmentation.

What?
-----

StopGap will fill up smallish free space areas (gaps) on NTFS drives.

It is not meant to defragment files (although it does to some extend)
nor is it mean to optimize the whole layout. Use other tools for these
tasks.

Why?
----

Common (free) defragmentation tools either do not support consolidating
free space (UltraDefrag e.g.), or are dead slow on large file systems
(Windows Defrag, MyDefrag, Defraggler).

I need to defrag space on some large partitions (500GB, 1,2 TB), where
due to the workload and usage pattern, the partitions contain millions
of differently sized files, with many deletes/truncates on many files,
often creating 100K+ tiny gaps in the layout aka. free space
fragmentation. This in turn will cause new/changed files to become
heavily fragmented (in the order of thousands of fragments), which of
course slows accesses down to a crawl.
Since the files change a lot, regular, "optimizing" defragmentation is
overkill, will take too long, and also performs far more writes then
necessary.

Hence StopGap.


State?
------

The code is currently alpha-quality. Expect bugs.
Although, by using the Windows Defrag API, there shouldn't be any data
loss issues (expect if your drive fails, of course).

Notes
-----

* StopGap makes use of zenwinx, which is part of the great UltraDefrag
  project.
* StopGap might use multiple passes.
* StopGap is currently single-threaded. While it would be possible to
  make parts of it multi-threaded, it isn't worth the fuzz: If StopGap
  operates as intented, most time will be I/O wait anyway, and doing
  multiple/overlapped IOOps in parallel isn't really helpful either.
* StopGap might initially cause more gaps to appear. This is expected
  and the gaps should disappear again as the operation progresses.
* You need the WDK to compile. And boost.
* Should compile using the MSVC and Intel compilers.
  Tested MSVC 2012 and Intel 14. As such, StopGap only uses C++11
  features supported by said compilers.
* If you experience bugs, do not expect me to fix them! I probably
  won't. This whole project is not a fulfledged end-comsumer product
  anyway. Having said that, sane patches are certainly welcome.
* Why I use zenwinx you ask? Because I'm too lazy to reinvent the wheel.
* Why I do not use udefrag.exe/udefrag.dll? Because I'm too lazy to hack
  this stuff in udefrag.
