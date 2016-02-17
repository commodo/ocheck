# ocheck
Tiny memleak checker library

Description
-----------
The name comes from [mcheck](http://man7.org/linux/man-pages/man3/mcheck.3.html), but the name was changed to prevent confusion.
The tool is meant to be used in OpenWRT, but can be extended/used in other contexts as well.

ocheck uses [libubox](http://git.openwrt.org/?p=project/libubox.git;a=summary) and [ubus](http://git.openwrt.org/?p=project/ubus.git;a=summary), so those will need to be built as well.

It works differently than mcheck. While mcheck needs glibc's malloc hooks, ocheck uses LD_PRELOAD which is supported by multiple libc implementations (including [musl](http://www.musl-libc.org/)).
The LD_PRELOAD env-var will override malloc, free (and friends) and keep a static table of allocated and free'd pointers and their respective backtraces.

Whenever something gets alloc-ed, and entry is added, whenever it's free'd it's removed. realloc() is special, since it may remove + add another entry.

The table has 32,768 entries (by default) which should be sufficient for most cases.

Every 512 allocs, the table will get flushed to ocheckd for inspection.
And after the program finishes it will also flush the table.

If the table is non-empty, the **ocheck\_fini()** destructor function will call a SEGV signal to the program to generate a coredump.
This method is a bit brutal, but it's comfortable when ananlyzing the backtraces.

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

* Start ocheckd in a background/daemonized process.
* Start your program with these params:
```
PROC=my_program LD_PRELOAD=/lib/libocheck.so my_program
```
* Stop your program gracefully (SIGTERM or SIGINT, or whatever helps to pass the execution through the cleanup code, before exiting)
* `ubus call ocheck list` to see what was un-free'd ; you can do this also while your program is running, but you'll get partial/runtime stats, which I am not sure is very helpful (in general)

If you want to clear any previous results:
* `ubus call ocheck clear` does that
* `ubus call ocheck clear '{ "name" : "my_program" }' ` will clear all results begin with "my\_program" 

Debugging the output
-----------
Once a coredump is generated, it means there's also some backtraces (or vice-versa).

Get the backtraces like this:

```
root@OpenWRT:/# ubus call ocheck list
{
        "my_program.5609": {
                "allocs": [
                        {
                                "tid": 5609,
                                "ptr": "0x104282c0",
                                "frame0": "0xb742c254",
                                "frame1": "0xb742ab28",
                                "frame2": "0xb742a9e4",
                                "frame3": "0xb7429f40",
                                "frame4": "0xb758fa5c",
                                "frame5": "0xb758fabc",
                                "frame6": "0x10001c84",
                                "frame7": "0xb74519ac",
                                "size": 11
                        }
                ],
                "count": 1
        }
}
```
**Notes:**
* for as long as ocheck runs, these backtraces will exist
* the key is the name of the program + the program's pid ; so "my\_program.5609" 
* **tid** == thread ID, which for single-threaded programs is the PID ; this tool does not support threads (yet), but there is intent to go there in the future

Open up a gdb on the generated coredump. An example:
```
~/work/openwrt/build_dir/toolchain-powerpc_8540_gcc-4.8-linaro_uClibc-0.9.33.2/gdb-7.8/gdb/gdb ~/work/openwrt/staging_dir/target-powerpc_8540_uClibc-0.9.33.2/root-mpc85xx/usr/bin/my_program my_program.5609.core< in gdb >
set sysroot ~/work/openwrt/staging_dir/target-powerpc_8540_uClibc-0.9.33.2/root-mpc85xx/
< then start having fun, by going through the backtraces/frames you got up here >
list *0xb74519ac
list *0x10001c84
....
list *0xb742c254  < in this case it's in uClibc @  libc/misc/time/time.c:632 >
```

Troubleshooting
---------------
* Q: I am getting false-positives even though the cleanup code is in place and is running.
* A: Check if you're using `popen()` and it's not segfaulting (due to whatever reason)

* Q: I am getting un-alloced pointers in libc (musl, uClibc, etc)
* A: Send a patch upstream to them and talk 

< I'll probably add a few more as this stuff develops >
For other stuff, just open an issue on Github, or email directly.
