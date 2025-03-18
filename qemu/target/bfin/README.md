# Blackfin QEMU Port

This is the port for QEMU to [Analog Devices, Inc.'s Blackfin processor](https://www.analog.com/en/products/processors-microcontrollers/processors-dsp/blackfin-embedded-processors.html).

It's a work in progress, and done in my idle time.  Which everyone has loads
and loads of, of course.  So things will get fleshed out as I find time for it.

## Status

The userland port should largely be working (see [Design Choices] for caveats).

The system port should not be relied upon for anything.  I'm still figuring the
various models out.  Don't even bother building it unless you plan on trying to
contribute.

## Testing

The only tests I actively run:
```
$ make -j4 -C tests/tcg/bfin/
```

Whether any other tests work, who knows!

## Design Choices

There are some things we don't bother handling in the port for speed reasons.
If you want an accurate (but not as fast) simulator, then use the GNU Simulator
as found in the GNU toolchain (part of GDB).

Things we do not currently handle by design:

* invalid parallel instruction combinations
  * no toolchain will output these
  * things like jumps

* invalid register combinations
  * some insns cannot have same register be both source and dest
  * no toolchain will output these

* transactional parallel instructions
  * on the hardware, if a load/store causes an exception, the other
    insns do not change register states either.  in qemu, they do,
    but since those exceptions will kill the program anyways, who
    cares.  no intermediate store buffers!

* AC0_COPY and V_COPY
  * no one has ever used these instead of AC0 or V

* no support for RND_MOD

There are a few insns/modes we don't currently handle, but it's more a matter
of nothing really uses these, so we haven't bothered.  If these matter to you,
then feel free to request support for them.

## History

The core of the Blackfin simulator (how to interpreter instructions and what
to do with them) is taken straight from the GNU Simulator, and then converted
to QEMU TCG operations.  I won't get into the history of the GNU Simulator,
so please refer to that project for more details.

(2008-2010)
Whilst employed at ADI and working on said GNU Simulator, I liked poking at
QEMU out of curiosity.  It took a while for me to understand how QEMU worked,
especially as documentation was non-existent.  For the next few years, I worked
on the GNU Simulator port to understand the underlying hardware architecture
more and more.  From time to time, I would revisit QEMU to see how things had
improved, and if I could chip away at its (undocumented) code.  As can be seen
from the dates above, it took a few years :P.

(early 2011) v0.14-bfin
An xmas miracle (or something)!  After another hacking session over the
2010-2011 holiday break, I finally got userland emulation somewhat working.
This came together enough to make into the first "release" with QEMU 0.14.

At this point, Blackfin FLAT & FDPIC ELF userland programs should work, as does
the TCG testsuite (whoo!).

(late 2011)
I left ADI and no longer worked on Blackfin as a job.  Now anything Blackfin
related becomes a hobby and free labor for ADI, if they even care about the
port anymore :P.

Since QEMU itself is still quite interesting, I use Blackfin as an excuse to
continue to learn about how QEMU works -- I am pretty well versed in the CPU
hardware, just not QEMU itself.

(mid 2012) v1.1.0-bfin
The userland portion is stable enough that I can start chipping away at the
system emulation.  Initial models are added here, but far from complete.  As
time goes on, I chip away at more and more.

(today)
As new release of QEMU come out, I try to rebase and keep at least the userland
code working.  The system stuff continues to be poked at.
