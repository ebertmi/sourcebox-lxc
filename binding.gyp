{
  "targets": [{
    "target_name": "lxc",
    "sources": [
      "src/lxc.cc",
      "src/async.cc",
      "src/get.cc",
      "src/create.cc",
      "src/destroy.cc",
      "src/clone.cc",
      "src/config.cc",
      "src/start.cc",
      "src/stop.cc",
      "src/attach.cc"
    ],
    "libraries": [
      "-lutil",
      "-llxc",
      "-lcap"
    ],
    "cflags": [
      "-std=c++11",
      "-Wpedantic"
    ],
    "include_dirs": [
      "<!(node -e \"require('nan')\")"
    ]
  }]
}
