(No changes to contents from v1, added signed-off-by's and fixed up commit messages)

Started this when writing tests for a feature I'm working on, needing a way to
read/write numbers to system files. After writing some utils to safely handle
file IO and parsing, I realised I'd made the ~6th file read/write implementation
and only(?) number parser that checks all the failure modes when expecting to
parse a single number from a file.

So these utils ended up becoming this series. I also modified some other test
utils I came across while doing so. My understanding is selftests are not expected
to be backported, so I wasn't concerned about only introducing new utils and leaving
the existing implementations be.

Changes:
- Use the mtfspr/mfspr macros where possible over inline asm
- Fix potential non-null terminated buffer in ptrace tests
- Add read_file / write_file to read and write raw bytes given appropriate
  path and buffers. Replace hand rolled read/write with this where easy.
- Make read/write_debugfs_file work on byte buffers and introduce
  read/write_debugfs_int for int specific contents. This more naturally aligns
  with the read/write_file functions, and allows for future *_long, *_ulong
  variants when required.
- Add an error checking number parser. It's an ugly function generating macro.
  The issue is the result param type can't be made generic, so there needs to
  be a separate definition per type (or at least for signed/unsigned). Also
  can't seem to use generics with the variable type declaration, so the max
  sized type for the input sign has to be specified manually.
  It's at least grep-able and language servers recognise it as defining
  parse_int, etc., though.
- Add the read_long, write_long, etc., utils that combine file IO and parsing.
  These are the utils I really wanted, useful for system files that are just
  numbers.
- Add an allocating file read for when the buffer is potentially too big to
  preallocate on the stack or needs to live especially long.
