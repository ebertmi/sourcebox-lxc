{
  "targets": [{
    "target_name": "lxc",
    "sources": [
      "src/lxc.cc",
      "src/async.cc",
      "src/get.cc",
      "src/start.cc",
      "src/stop.cc",
      "src/destroy.cc",
      "src/clone.cc",
      "src/attach.cc"
    ],
    "libraries": [
      "-lutil",
      "-llxc"
    ],
    "cflags": [
      "-std=c++11",
      "-Wpedantic"
    ],
    "include_dirs": [
      "<!(node -e \"require('nan')\")"
    ]
  },
  {
    "target_name": "sourcebox-init",
    "type": "executable",
    "sources": [
      "src/init.c"
    ],
    "cflags!": [
      "-pthread"
    ],
    "ldflags!": [
      "-pthread"
    ]
  }]
}
