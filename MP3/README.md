# CS423-MP1 by Boyin Zhang(bzhang70)

## Files

* `mp3.c` the core file containing all the methods for this MP.
* `processing_data.ipynb` the python notebook to plot figures for set of data.
* `CS423_MP3_bzhang70.pdf` the report for this MP, including implementation details, design decisions, figures and analysis.
* `Makefile` makefile of this MP, compile `mp3.c`,`work.c` and `monitor.c`

## How to run the program

1. `make` to compile all the files and dependencies.
2. `sudo insmod BZHANG70_MP3.ko` install the module to kernel.
3. `cat /proc/devices` to check `mp3` and then `mknod node c #mp3 0`.
4. run `nice ./work 1024 R 50000 & nice ./work 1024 L 10000 &` or more apps for multiple tasks.
5. `sudo ./monitor > profile1.data` and then you can use ipython script `processing_data.ipynb` to plot the figures.
6. After all the things done, by `sudo rmmod BZHANG70_MP2` to remove the module.
7. `make clean`.

## Implementation and Degisning

Please see `CS423_MP3_bzhang70.pdf` for details.
