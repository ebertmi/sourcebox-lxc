'use strict';

var util = require('util');

exports.errnoException = function (errno, syscall, path) {
  errno = -errno;
  var err;

  if (util._errnoException) {
    err = util._errnoException(errno, syscall);
  } else {
    err = new Error(syscall + ', errno ' + errno);
      err.syscall = syscall;
      err.code = errno;
      err.errno = errno;
  }

  if (path) {
    err.path = path;
  }

  return err;
};
