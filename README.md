# ocheck
Tiny memleak checker library

Description
-----------
The name comes from [mcheck](http://man7.org/linux/man-pages/man3/mcheck.3.html), but the name was changed to prevent confusion.
The tool is meant to be used in OpenWRT, but can be extended/used in other contexts as well.

It works differently than mcheck. While mcheck needs glibc's malloc hooks, ocheck uses LD_PRELOAD which is supported by multiple libc implementations (including [musl](http://www.musl-libc.org/)).
The LD_PRELOAD env-var will override malloc, free (and friends) and keep a static table of allocated and free'd pointers and their respective backtraces.

Whenever something gets alloc-ed, and entry is added, whenever it's free'd it's removed. realloc() is special, since it may remove + add another entry.

For this leak-checking method to work well, the program will need to do extensive cleanup before exiting.
So, the user will need to free() all alloc-ed pointers, call all cleanup functions for (like for example OpenSSL's call **CRYPTO\_cleanup\_all\_ex\_data()** should help), close syslogs, etc.
Otherwise, there will be quite a few false-positives, the number of false positives might never be the same, so it's hard to quantify (and hence test) them.
Of course there are a few cases where this can't be done, or is hard to do, like dynamically alloc-ed lists/tables/caches in libc.
These would need to be handled on a per-case basis, and maybe patch the libc's with a **libc\_cleanup()** function.

**Note:** Execution speed is decreased by a factor of 2x to 3x (depends), so the use of this tool is not recommended for production cases.

Motivation
----------
So, some will say: there's Valgrind that does this.

And I'll say: yeah, but Valgrind doesn't work well on PowerPC (because it does not recognize all instruction sets yet).

How To Use
-----------

* Start your program with these params:
```
PROC=my_program LD_PRELOAD=/lib/libocheck.so my_program
```
* Stop your program gracefully (SIGTERM or SIGINT, or whatever helps to pass the execution through the cleanup code, before exiting)
* Check output files `/tmp/ocheck.out` & `/tmp/ocheck.leaks`

Supported env-vars
-----------

* **PROC** - specify the program name ; it's required, to avoid doing "smart" program name obtaining ; we're in a LD_PRELOAD lib and this can be tricky with some libc's
* **DEBUG_OUTPUT** - override default output file ; default  `/tmp/ocheck.out`
* **LEAKS_OUTPUT** - override default leaks report file ; default `/tmp/ocheck.leaks`

