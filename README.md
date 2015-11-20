sourcebox/lxc
=============

LXC bindings for Node.js.

For now, this module does not implement the full API exposed by LXC, but only
the subset that is required to provide a sandboxed environment.

## Requirements

### LXC

A recent version of `lxc` is required. Depending on your distro, installing
`lxc-dev` might also be required.

Note that Debian currently does not provide a `lxc-dev` package, so you probably
want to compile LXC from source:

```
git clone git@github.com:lxc/lxc.git
cd lxc
./autogen.sh
./configure
make
sudo make install
```

### Node.js

A recent version (preferably 0.12.0+) of Node.js is recommended.

The bindings might work with older versions (0.10.xx), but a bug in libuv
prevents the correct reaping of child processes, especially when used in
conjunction with the `child_process` module. This bug was fixed in Node.js
0.11.13.

The old libuv version will reap all child processes, regardless of if they were
spawned by libuv itself or another native module. That means there is a good
chance that you will not receive any `exit` or `close` events for attached
processes.

You have been warned.

### libcap-dev

Library for setting POSIX capabilities.
