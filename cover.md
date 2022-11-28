Add DEXCR support

This series is based on initial work by Chris Riedl that was not sent
to the list.

Adds a kernel interface for userspace to interact with the DEXCR.
The DEXCR is a SPR that allows control over various execution
'aspects', such as indirect branch prediction and enabling the
hashst/hashchk instructions. Further details are in ISA 3.1B
Book 3 chapter 12.

This RFC proposes an interface for users to interact with the DEXCR.
It aims to support

* Querying supported aspects
* Getting/setting aspects on a per-process level
* Allowing global overrides across all processes

There are some parts that I'm not sure on the best way to approach (hence RFC):

* The feature names in arch/powerpc/kernel/dt_cpu_ftrs.c appear to be unimplemented
  in skiboot, so are being defined by this series. Is being so verbose fine?
* What aspects should be editable by a process? E.g., SBHE has
  effects that potentially bleed into other processes. Should
  it only be system wide configurable?
* Should configuring certain aspects for the process be non-privileged? E.g.,
  Is there harm in always allowing configuration of IBRTPD, SRAPD? The *FORCE_SET*
  action prevents further process local changes regardless of privilege.
* The tests fail Patchwork CI because of the new prctl macros, and the CI
  doesn't run headers_install and add -isystem <buildpath>/usr/include to
  the make command.
* On handling an exception, I don't check if the NPHIE bit is enabled in the DEXCR.
  To do so would require reading both the DEXCR and HDEXCR, for little gain (it
  should only matter that the current instruction was a hashchk. If so, the only
  reason it would cause an exception is the failed check. If the instruction is
  rewritten between exception and check we'd be wrong anyway).

The series is based on the earlier selftest utils series[1], so the tests won't build
at all without applying that first. The kernel side should build fine on ppc/next
247f34f7b80357943234f93f247a1ae6b6c3a740 though.

[1]: https://patchwork.ozlabs.org/project/linuxppc-dev/cover/20221122231103.15829-1-bgray@linux.ibm.com/
