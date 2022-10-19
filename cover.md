This is a revision of Chris and Jordan's series to introduces a per cpu temporary
mm to be used for patching with strict rwx on radix mmus.

It is just rebased on powerpc/next. I am aware there are several code patching
patches on the list and can rebase when necessary. For now I figure this'll get
changes requested for a v9 anyway.

v8:	* Merge the temp mm 'introduction' and usage into one patch.
	  x86 split it because their temp MMU swap mechanism may be
	  used for other purposes, but ours cannot (it is local to
	  code-patching.c).
	* Shuffle v7,3/5 cpu_patching_addr usage to the end (v8,4/4)
	  after cpu_patching_addr is actually introduced.
	* Clearer formatting of the cpuhp_setup_state arguments
	* Only allocate patching resources as CPU comes online. Free
	  them when CPU goes offline or if an error occurs during allocation.
	* Refactored the random address calculation to make the page
	  alignment more obvious.
	* Manually perform the allocation page walk to avoid taking locks
	  (which, given they are not necessary to take, is misleading) and
	  prevent memory leaks if page tree allocation fails.
	* Cache the pte pointer.
	* Stop using the patching mm first, then clear the patching PTE & TLB.
	* Only clear the VA with the writable mapping from the TLB. Leaving
	  the other TLB entries helps performance, especially when patching
	  many times in a row (e.g., ftrace activation).
	* Instruction patch verification moved to it's own patch onto shared
	  path with existing mechanism.
	* Detect missing patching_mm and return an error for the caller to
	  decide what to do.
	* Comment the purposes of each synchronisation, and why it is safe to
	  omit some at certain points.

Previous versions:
v7: https://lore.kernel.org/all/20211110003717.1150965-1-jniethe5@gmail.com/
v6: https://lore.kernel.org/all/20210911022904.30962-1-cmr@bluescreens.de/
v5: https://lore.kernel.org/all/20210713053113.4632-1-cmr@linux.ibm.com/
v4: https://lore.kernel.org/all/20210429072057.8870-1-cmr@bluescreens.de/
v3: https://lore.kernel.org/all/20200827052659.24922-1-cmr@codefail.de/
v2: https://lore.kernel.org/all/20200709040316.12789-1-cmr@informatik.wtf/
v1: https://lore.kernel.org/all/20200603051912.23296-1-cmr@informatik.wtf/
RFC: https://lore.kernel.org/all/20200323045205.20314-1-cmr@informatik.wtf/
x86: https://lore.kernel.org/kernel-hardening/20190426232303.28381-1-nadav.amit@gmail.com/
