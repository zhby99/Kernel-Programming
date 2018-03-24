# CS423-MP1 by Boyin Zhang(bzhang70)

## Files

* `mp2.c` the core file containing all the methods for this MP.
* `userapp.c` the user application file, which is doing the factorial computation. And the input parameter for this file is `period`, `cpu time` and `iterations`.
* `Makefile` makefile of this MP, compile both `mp2.c` and `userapp.c`

## How to run the program

1. `make` to compile all the files and dependencies.
2. `sudo insmod BZHANG70_MP2.ko` install the module to kernel.
3. run `./userapp period1 cpu_time1 iterations1 & ./userapp period2 cpu_time2 iterations2` or more apps for multiple tasks.
4. `cat /proc/mp1/status` to check `PID : CPU_TIME`.
5. After all the things done, by `sudo rmmod BZHANG70_MP1` to remove the module.
6. `make clean`.

## Screenshot for 2 processes with 2000 period and 1000 period

After running `./userapp 2000 200 50 & ./userapp 1000 200 100`, we get the screenshot as following.

![Screenshot](./sshot.png)
