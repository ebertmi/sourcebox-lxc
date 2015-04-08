{
  "targets": [{
    "target_name": "lxc",
    "sources": [
      "src/lxc.cc",
      "src/get.cc",
      "src/attach.cc"
    ],
    "libraries": [
      "-lutil",
      "-llxc"
    ],
    "include_dirs": [
      "<!(node -e \"require('nan')\")"
    ]
  }]
}
