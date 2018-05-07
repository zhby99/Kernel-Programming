# CS423-MP4 by Boyin Zhang(bzhang70)

## Files

* `mp4.c` the core file containing all the methods for this MP.
* `Makefile` makefile of this MP.

## Implementation and Degisning

1. Since I don't have chance to run my code because of the crash of the kernel and down of VSphere, this is just a draft version to submit according to TA's instruction on Piazza.
2. For `mp4_cred_prepare()`, `mp4_cred_free()` and `mp4_cred_alloc_blank()`, I followed the instructions, to allocate heap memory for `struct mp4_security` and then link them with the `security` pointer of `cred`.
3. For `mp4_bprm_set_creds()`, I got `sid` of the `bprm` by calling the helper function `get_inode_sid()` and then compare `sid` with `MP4_TARGET_SID`, if it is equal, I set the label of the bprm's credential to be also `MP4_TARGET_SID`.
4. For `mp4_inode_init_security()`, I call `S_ISDIR()` to know whether the inode is directory or file, after that, I set the attribute and name for the inode.
5. For `mp4_inode_permission()`, I get osid and ssid, and then call the helper function `mp4_has_permission()` which is designed according to the instructions to get the result.  
