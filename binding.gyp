{
  "targets": [{
    "target_name": "lxc",
    "sources": [
      "src/lxc.cc"
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
