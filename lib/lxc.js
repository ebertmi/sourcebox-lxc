var events = require('events');
var net = require('net');
var util = require('util');

var extend = require('extend');
var binding = require('bindings')('lxc.node');

var attachedProcesses = {};

function reapAttachedProcesses() {
  var result = binding.waitPids(Object.keys(attachedProcesses));

  if (result) {
    var attachedProcess = attachedProcesses[result.pid];
    delete attachedProcesses[result.pid];
    if (result.signalCode) {
      attachedProcess.signalCode = result.signalCode;
    } else {
      attachedProcess.exitCode = result.exitCode;
    }

    attachedProcess.stdin.destroy();

    if (result.exitCode == 128) {
      attachedProcess.emit('error', new Error('Failed to execute "' +
                                              attachedProcess.spawnfile  + '"'));
    } else {
      attachedProcess.emit('exit', attachedProcess.exitCode, attachedProcess.signalCode);
    }

    maybeClose(attachedProcess);

    // TODO stream cleanup?
    // flush stdio? see child process api
  }
}

process.on('SIGCHLD', reapAttachedProcesses);

function maybeClose(attachedProcess) {
  if (++attachedProcess._closesGot === attachedProcess._closesNeeded) {
    attachedProcess.emit('close', attachedProcess.exitCode, attachedProcess.signalCode);
  }
}

/**
 * @class
 * @private
 */
function TTYStream(options) {
  var tty = process.binding('tty_wrap');
  var guessHandleType = tty.guessHandleType;

  tty.guessHandleType = function () {
    return 'PIPE';
  };

  TTYStream.super_.call(this, options);
  tty.guessHandleType = guessHandleType;

  this.on('error', function (err) {
    if (err.code === "EIO") {
      // child exited
      return;
    }

    if (this.listeners('error').length <= 1) {
      throw err;
    }
  });
}

util.inherits(TTYStream, net.Socket);

TTYStream.prototype.resize = function (cols, rows) {
  return binding.resize(this._handle.fd, cols, rows);
};

/**
 * Not intended to be used directly.
 *
 * @class
 * @protected
 */
function AttachedProcess(command, fds, term, container) {
  AttachedProcess.super_.call(this);

  this._closesGot = 0;
  this._closesNeeded = fds.length;

  this.exitCode = null;
  this.signalCode = null;

  // node child API compatibility
  this.spawnfile = command;

  // container this process belongs to
  this.container = container;

  // Process ID of attached process. May not be available immediately since
  // attachment is asynchronous.
  this.pid = null;

  this.stdio = [];

  fds.forEach(function (fd, i) {
    var stream;

    if (i < 3 && term) {
      if (i === 0) {
        stream = new TTYStream({
          fd: fd,
          readable: true,
          writable: true
        });
      } else {
        stream = this.stdio[0];
      }
    } else {
      stream = new net.Socket({
        fd: fd,
        readable: i > 0,
        writable: i === 0 || i > 2
      });
    }

    if (i > 0) {
      stream.on('close', maybeClose.bind(null, this));
    }

    this.stdio[i] = stream;
  }, this);

  this.stdin = this.stdio[0];
  this.stdout = this.stdio[1];
  this.stderr = this.stdio[2];
}

util.inherits(AttachedProcess, events.EventEmitter);

/**
 * Send a signal to the child process. If no argument is given, the process
 * will be sent `'SIGTERM'`. See `signal(7)` for a list of available signals.
 *
 * Note that most shells will ignore or internally handle signals like
 * `SIGTERM` and `SIGQUIT`.
 *
 * @param {String} [signal=SIGTERM] Signal to send
 */
AttachedProcess.prototype.kill = function (signal) {
  if (this.pid === null) {
    throw Error('Process not attached');
  }

  try {
    return process.kill(this.pid, signal);
  } catch (err) {
    if (err.code === undefined || err.code === 'EINVAL' || err.code === 'ENOSYS' ) {
      // unknown or unsupported signal
      throw err;
    } else if (err.code === 'ESRCH') {
      // child is already dead
    } else {
      this.emit('error', err);
    }
  }

  return false;
};

/**
 * @class
 * @protected
 */
function Container(container) {
  this._container = container;
  this._container.owner = this;
}

// not finished
Container.prototype.start = function (command, args) {
  return this._container.start([].concat(command, args));
};

Container.prototype.state = function () {
  return this._container.state();
};

/**
 * @returns {AttachedProcess}
 */
Container.prototype.attach = function (command, args, options) {
  var defaults = {
    cwd: '/',
    env: process.env,
    term: false,
    fds: 0
  };

  options = extend(defaults, options);

  var envPairs = [];

  for (var key in options.env) {
    envPairs.push(key + '=' + options.env[key]);
  }

  options.env = envPairs;

  function callback(err, pid) {
    if (err) {
      attachedProcess.emit('error', err);
    } else {
      attachedProcesses[pid] = attachedProcess;
      attachedProcess.pid = pid;
      attachedProcess.emit('attach', pid);
    }
  }

  var fds = this._container.attach(command, args, options, callback);
  var attachedProcess = new AttachedProcess(command, fds, options.term, this);

  return attachedProcess;
};

Container.prototype.destroy = function (callback) {
  this._container.destroy(callback);
};


module.exports = function (name, path, callback) {
  binding.getContainer(name, path, function (err, container) {
    if (err) {
      return callback(err);
    }

    callback(null, new Container(container));
  });
};
