Use cases:

1. Program sets its DEXCR up front and expects it to be copied to all threads
2. Program sets a DEXCR it wants inherited by an exec-ed program (that may not
	itself be DEXCR aware). * E.g., the init process can configure the DEXCR
	for all non-DEXCR aware programs
3. Program wants its own DEXCR, but doesn't require it be inherited by any
   exec-ed children
4. Program wants to disable its NPHIE aspect but we don't want to allow it to
	disable exec-ed children's NPHIE * E.g., `disable_nphie sudo foo`
5. Admin wants to lock down aspect (e.g., indirect branch prediction...) * E.g.,
	"New speculative CVE just dropped, we need to disable X for all
	processes ASAP". Sure, just write "1" to /sys/kernel/dexcr/X * Need a
	generic system interface for any aspect to be globally set and enforced
	* The process inheritance kind of does this, but doesn't enforce it. It
	might also be difficult for an admin to actually modify the init task to
	do this, compared to a service that can be registered with systemd. *
	Removes need for specific "enforce NPHIE" config too. * Can mimic DEXCR
	design, be a simple OR with the system value. Can only enforce bits are
	set, not unset.


Scenarios:

1. Malicious program running as some user, wants root access. * Sudo compiled
	with hashchk for increased security * Program can just disable NPHIE and
	have it be inherited * Then sudo isn't really protected... unless it
	explicitly sets its DEXCR


Other design points:

1. The kernel will only use a single DEXCR value. This avoids requiring
	synchronization when the DEXCR is changed in kernel space; it
		automatically synchronizes the userspace aspects when returning
		to userspace. Most relevant for switching tasks in
		restore_sprs() * No need to use write_dexcr() there either. We
		are in an interrupts disabled context, so any change to the
		system settings will either have already occurred, or a pending
		interrupt will play once we're done. * Why am I dumb. If the
		waking thread doesn't agree with the existing DEXCR value, it
		needs to apply a potentially changed systm config _from when it
		went to sleep_ ...


Ultimately this interface is to solve two design goals:

1. Process sets its own/descendants DEXCR value
2. Admin can set DEXCR values system wide






Focused on the following goals:

1. Admin has system wide control. If they say "enable aspect" then all processes
   get it enabled.
2. Process has self control. Setting is inherited across fork. * Mostly free for
	process to toggle (still overridden by any admin level setting), but...
	* "Turning hash checks off across exec requires admin rights" * This
	prevents a malicious user from disabling hash checks on a setuid program
	as part of a privilege escalation exploit.


Design decisions:

1. To reconcile a task local config + global override, we need to store the task
   config. This is separate to the serialized 'actual dexcr' when the task is
   put to sleep (which we still need for old-new comparison).
2. There is no global disable of an aspect. Is it necessary? Suppose it turns
	out enabling an aspect is a security issue. How does admin lock down
	system? * Should really provide global disable of aspect. So same kernel
	can be used without patching.




--------------------------------------------------------------------------------

Research:

# NPHIE analogs

## ARM Pointer Authentication (PAC)

https://www.kernel.org/doc/html/next/arm64/pointer-authentication.html

1. PAC state is inherited across fork, reset on exec.
2. PAC defaults enabled on exec, so no need to expose a check status.

https://lore.kernel.org/linux-arm-kernel/20200707173232.5535-1-steve.capper@arm.com/T/#u

Kernel is generally against global controls like this; if the program is buggy,
recompile the program without it. Considered a bad precedent.

https://fedoraproject.org/wiki/Changes/Aarch64_PointerAuthentication

Fedora went ahead and merged it. Their contingency plan is to monitor for and
fix issues as they appear. Anything not in a core package is not a blocker.

Key quotes:

> > I am trying to enable pointer authentication in distros. One concern I have
> > is that a pointer auth bug could slip through the cracks (with a lot of
> > hardware not yet supporting pointer auth), and then affect an end user.
>
> Why is that different to any other feature we expose to userspace? Bugs
> happen, and we deal with them.

> > Also, I have had interest from distros in the performance cost of pointer
> > auth, and there will very likely be folk switching it off/on again in order
> > to perform tests.
>
> And they can do that with the compiler at the same time as they pass
> -funsafe-math -Ofast.

^ This one is interesting. ROP protection isn't just whether hashchk is a NOP,
but changes what the compiler can optimise. So there are three scenarios to
consider for performance here: no hashchk, hashchk but NPHIE disabled, and
hashchk with NPHIE.


> > Having a mechanism in the kernel that an end user can employ to activate/
> > de-activate pointer auth would help with deployment greatly, and that is
> > what I was trying to achieve with this patch.
> >
> > Another way to approach this could be via a kernel command line that
> > completely disables pointer auth? (i.e. kernel not activating pointer auth
> > at all, and userspace not seeing the feature)
>
> I did wonder briefly about overriding the sanitised ID registers on the
> command-line, but I think it opens a door that we'll regret opening later on.


But distros might need it anyway; likened to selinux=0

> So, a bit of background. Pointer auth has been accepted as a systemwide change
> for Fedora 33. That acceptance requires contingency planning for what to do if
> the feature is causing ship blocking/etc defects. While we are going to do our
> best to test it before release, as your aware, the 8.3+ machines are in short
> supply. Most of the community testing & debugging will simply be to assure
> that we don't break anything on v8.0 machines. Which means that its quite
> likely that the bugs are going to be reported by end users with 8.3+ hardware
> who don't compile their own packages, nor debug them (and the idea that we can
> get any kind of coverage on tens of thousands of packages is crazy). Given
> many of the bugs so far have been subtle glibc/gcc/etc they are also far
> ranging. So, being able to give the end user a simple way to a->b test whether
> a problem goes away helps us to triage whether we are looking at a generic
> bug, or one related to pointer auth (or for that matter an active attack).
> Further, should it happen to something during the boot process (which is
> significantly more complex on fedora than the usual kernel+busybox+disk image)
> then we need a straightforward method to boot the end users machines so they
> can in-place install/upgrade/test.
>
> In summary I see this sort of similar to selinux=0, a flag that no one should
> be using, but is still frequently used because its required just to boot a
> machine in order to fix a problem.

Points:

1. Possible subtle bugs in the ecosystem (glibc, gcc, etc.) possible
2. Need mechanism for a/b testing if end users report issues


Current situation:

1. Enabled on exec
2. Can be altered with prctl
3. No global controls

#### Comparison

1. PAC state is inherited across fork, resets to all enabled on exec.
  * Sidesteps the setuid inheritance issue * There is some kind of shim making
    decisions about enablement? How do they disable it for a faulty program?
  * Also, the actual faulty program could be several layers deep. Any solution
    for a/b testing needs to have confidence that it can control these deeper
    programs.

2. PAC defaults enabled on exec, so no need to expose a check status.

3. Global PAC design would only apply to newly exec'd processes. Seems better
   than applying to all current process as the DEXCR patch currently does.


# Speculation controls

1. Speculation is not a property of the binary; you don't compile a program
   differently.

2. In general, it's fine for a process to control its speculation value
   Particularly sensitive programs may want the added confidence, others may
   want best performance.

3. But if there's a CVE there needs to be a way to enforce that all processes
   run with certain aspect value.

5. Possible mechanisms
  * Hypervisor enforced
    - Does PHYP support this?
  * Patch and recompile kernel
    - Requires kernel update
  * Command line arg
    - Adding new args is unfavoured, but best coverage
  * Sysctl
    - Flexible, misses kernel and early userspace boot


# Design goals

1. Mechanism to control aspects for arbitrarily nested programs
2. Mechanism to enforce the speculation aspects system wide
3. Mechanism to disable ROP protection for a process
4. Mechanism to enforce ROP protection system wide


# Current design questions

1. Use case for 'locked' sysctl variants?
  * Motivation was nebulous 'increased confidence' in the application of the
    aspect.
  * But really, what threat model involves compromised sysctls? Surely you're
    already compromised beyond what speculation and ROP controls give you?
  * Also bad from a testing standpoint; you can't test if the lock is working
    without breaking subsequent test runs on that boot.
  * I'm inclined to remove them from the design.

2. What if system can't boot at all due to ROP protection?
  * Can install a wrapper init that disables ROP protection with inheritance,
    then execs the regular init.

3. What if a program is unconditionally enabling ROP protection?
  * The inherited setting is useless then.
  * The global override is the only way to test with it disabled.

4. How does a distro want to address ROP protection bugs in
  * A specific package (e.g., weird stack usage)
  * System wide (e.g., compiler bug)

5. How to test performance impact of ROP protection?
  * Recompiling without ROP protection gives the wholistic performance cost, but
    doesn't control for the difference in compiled assembly.
  * Is this performance testing something the distro can do with its own local
    patch/module?

# Design solutions

1. Track process specific DEXCR modified with prctl.
  * Provides flexibility when otherwise

2. Track generic system wide aspect override with sysctl.
  * Provides control over the process specific changes
  * Missing startup should be fine, as init scripts should be running well
    before any applications (which I assume are the risk)

Disabling ROP protection system wide (potentially for new processes) is just a
side effect of requiring to be able to disable ROP for any process for
debugging, even if some process higher in the chain enabled it.


# Status quo

NPHIE aspect is enabled in upstream and RHEL 9.4

Distros can already test what happens when enabling ROP protection in packages

Processes can't affect the DEXCR


# This series

prctl speculation controls will come in this series



# Categories

## Speculation (no architecture)

* Local controls good
* Global controls not a problem (useful even for SBHE)


## ROP protection

* Local controls good
* Global controls useful for debugging/performance testing
  * Debugging can be done by recompiling without ROP protection? Or just
    patching to disable own NPHIE in main()
  * How different is it from any other bug? Does it even help with triage? You
    can tell it was hashchk through signal + instruction location. If so, you
    could just recompile without ROP protection while investigating.



















I've laid out my thoughts and research on similar features in other arches below.

==============
ROP Protection
==============

ARM Pointer Authentication
--------------------------

A close analogue of the DEXCR ROP protection is ARM's Pointer Authentication
(PAC) [1]. ARM provides a local prctl control over the process' own PAC. The PAC
state is inherited across fork/clone, and resets to enabled on exec.

There was a proposal for global PAC control with sysctl in [2]. However it was
NAK'd with the reasoning that ROP protection bugs are just like any other
userspace bugs, and don't need special kernel handling. Someone commented on how
Fedora could benefit from the patch to allow faster bug triage and recovering a
system somehow bricked by ROP protection.

Fedora appears to have enabled PAC for their packages without this global
override, and I guess it's not had any significant issues.

[1]: https://www.kernel.org/doc/html/next/arm64/pointer-authentication.html
[2]: https://lore.kernel.org/linux-arm-kernel/20200707173232.5535-1-steve.capper@arm.com


PowerPC DEXCR
-------------

The NPHIE (ROP protection) aspect is practically the same as the PAC above.
Therefore the consistent thing to do would be to always enable NPHIE on exec and
let a process disable it with prctl if necessary. This is a natural extension of
the current static behaviour of "NPHIE always on".

Regarding the two concerns for the PAC above:
1. Triage is trivial anyway when you see SIGILL + faulting instruction is
   hashchk.

   If you need to test what happens when NPHIE is disabled for a process tree,
   is it possible to emulate it by ptrace-ing and running the disable prctl
   after every exec?

2. Can a system can be bricked by a ROP protection bug that isn't caught in
   testing? I can see how some non-core packages might not have test coverage,
   but they can be patched as needed? A recovery image could also just not
   enable ROP protection instructions.


===================
Speculation Aspects
===================

Prior Art: PR_GET_SPECULATION_CTRL
----------------------------------

Commit b617cfc858161140d69cc0b5cc211996b557a1c7
Commit a73ec77ee17ec556fe7f165d00314cb7c047b1ac

https://lore.kernel.org/all/1547676096-3281-1-git-send-email-longman@redhat.com/T/#u

There is a semi-generic prctl for speculation aspects already: PR_GET_SPECULATION_CTRL/PR_SET_SPECULATION_CTRL. This covers indirect branch prediction. Subroutine return address prediction would beed to be added as a separate entry, unless we tie it with indirect branch predition?

Either way, it's not considered important to provide a global override.

Clear on exec useful



These should be less of a concern, because they don't have any architectural
affects.

Like ROP protection, the default behaviour would be to let a process control its
own speculation aspects. Inheriting them across fork is natural. Inheriting them
across exec shouldn't be a problem; if a process disables speculation for
increased security, the default assumption would be it wants any processes it
spawns to also be more secure. If it wants fine-grained control it could always
reset the aspect value between the fork and exec.

My concern is what happens if a CVE is discovered that requires setting an
aspect to a certain value. The options I considered are
1. Hypervisor enforces aspect. All aspects can be set by a hypervisor as it
   likes. I don't know if PHYP supports this though?
2. Patch and release new kernel that freezes the aspect value.
3. Sysctl global override. Dynamic, and even automatically comes with a kernel
   boot flag.

Unlike ROP protection, I don't think a sysctl is bad for speculation related
aspects.


============
Proposed API
============

With the above in mind, my proposed API is




------------------------------------------------

## Design

The DEXCR is tracked as the live value in the SPR. There are no global modifications.


### Setup

DEXCR is initialised in init/restore CPUs for P10. As above, this makes it the initial value for all processors (secondaries do not restore from the task state anyway). We also use this default value as the default reset-on-exec value. So if nothing changes, the behaviour is exactly the same as with the current static setting.

If a process changes its own DEXCR, that applies across forks but is reset back to the default on exec.

If a process changes the reset value, its local value does not change. However any descendent execs will be loaded with the modified reset value.


### Examples

System wide default change -> set the rest in the init process

Container specific default change -> set the reset in the container root


