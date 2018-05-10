# CS423-MP4 by Boyin Zhang(bzhang70)

## Files

* `mp4.c` the core file containing all the methods for this MP.
* `Makefile` makefile of this MP.

## Implementation and Degisning

1. For `mp4_cred_prepare()`, `mp4_cred_free()` and `mp4_cred_alloc_blank()`, I followed the instructions, to allocate heap memory for `struct mp4_security` and then link them with the `security` pointer of `cred`.
2. For `mp4_bprm_set_creds()`, I got `sid` of the `bprm` by calling the helper function `get_inode_sid()` and then compare `sid` with `MP4_TARGET_SID`, if it is equal, I set the label of the bprm's credential to be also `MP4_TARGET_SID`.
3. For `mp4_inode_init_security()`, I call `S_ISDIR()` to know whether the inode is directory or file, after that, I set the attribute and name for the inode.
4. For `mp4_inode_permission()`, I get osid and ssid, and then call the helper function `mp4_has_permission()` which is designed according to the instructions to get the result.  

## Testing
For this part, I am only showing the operation to write on a read-only file.
```
bzhang70@sp18-cs423-013:~$ cd scripts/
bzhang70@sp18-cs423-013:~/scripts$ source test.perm
[sudo] password for bzhang70:
bzhang70@sp18-cs423-013:~/scripts$ echo "hard" > test.txt
-bash: test.txt: Permission denied
bzhang70@sp18-cs423-013:~/scripts$ source test.perm.unload
bzhang70@sp18-cs423-013:~/scripts$ echo "hard" > test.txt
```
As you can see, permission denied. And after we run the script to unload the settings, we can write to the file.

## Least privilege
You can check my `passwd_open.txt` file for all the commands generate by , and the `passwd.perm` and `passwd.perm.unload` for the scripts.
