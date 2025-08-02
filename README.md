# MyRAMFS – Minimal RAM-backed Filesystem for Linux

**MyRAMFS** is a custom Linux kernel module that implements a minimal in-memory virtual filesystem. It is writable, dynamically resizable, and works with Linux kernel 6.12+.

## Features

- Supports file creation, reading, writing, and truncation  
- Supports directory creation  
- Minimal codebase and easy to read  
- Fully lives in memory - no disk writes
- Kernel 6.12-compatible  

## Not Yet Implemented

- `unlink`, `rmdir` (file/directory deletion)
- Symbolic links or special files (e.g., sockets, fifos)
- Memory usage accounting or size limits
- Permissions enforcement beyond basic mode bits
- Integration with user namespaces or mount ID maps beyond `nop_mnt_idmap`

---

## Build Instructions (NixOS)

Ensure you have `nix` installed and a working NixOS setup with matching kernel headers.

```sh
# Inside the project directory
nix-build
````

This will produce a `.ko` file in:

```
./result/lib/modules/<your-kernel-version>/extra/myramfs.ko
```

If you already tried to load a module, unload it:

```sh
sudo rmmod myramfs || true
```

Now you can insert a newly built module:
```sh
sudo insmod result/lib/modules/$(uname -r)/extra/myramfs.ko
```

To verify that it's loaded, run
```sh
lsmod | grep myramfs
```

---

## Mount the Filesystem

Make a mount point if it doesn't exist:

```sh
sudo mkdir -p /mnt/rfs
```

Mount it:

```sh
sudo mount -t myramfs none /mnt/rfs/
```

Now you can use `/mnt/rfs` like a normal writable directory.

Example:

```sh
echo "Hello, RAM!" | sudo tee /mnt/rfs/hello.txt
sudo cat /mnt/rfs/hello.txt
```

---

## Unmount and Unload

To clean up:

```sh
sudo umount /mnt/rfs
sudo rmmod myramfs
```
