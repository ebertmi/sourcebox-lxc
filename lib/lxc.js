var events = require('events');
var net = require('net');
var util = require('util');

var extend = require('extend');

var binding = require('bindings')('lxc.node');

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
 * @class
 * @protected
 */
function AttachedProcess(command, pid, fds, term, container) {
  AttachedProcess.super_.call(this);

  this.spawnfile = command;

  this.container = container;
  this.pid = pid;
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
  try {
    return process.kill(this.pid, signal);
  } catch (err) {
    if (err.code === undefined ||err.code === 'EINVAL' || err.code === 'ENOSYS' ) {
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
 * @private
 */
AttachedProcess.prototype.onExit = function (exitCode, signalCode) {
  if (signalCode) {
    this.signalCode = signalCode;
  } else {
    this.exitCode = exitCode;
  }

  this.stdin.destroy();

  if (exitCode == 128) {
    this.emit('error', new Error('Failed to execute "' + this.spawnfile  + '"'));
  } else {
    this.emit('exit', this.exitCode, this.signalCode);
  }

  // TODO stream cleanup?
  // flush stdio? see child process api
};

/**
 * @class
 * @protected
 */
function Container(container) {
  this._container = container;
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

  var result = this._container.attach(command, args, options);
  // hier vielleicht der native methode den AttachedProcess übergeben??
  var attached = new AttachedProcess(command, result.pid, result.fds || [], options.term, this);

  // vielleicht das alles im AttachedProcess constructor
  if (result.error) {
    process.nextTick(function () {
      attached.emit('error', result.error);
    });
  } else {
    processes[attached.pid] = attached;
  }

  return attached;
};

var processes = {};

function waitPids() {
  var result = binding.waitPids(Object.keys(processes));

  if (result) {
    processes[result.pid].onExit(result.exitCode, result.signalCode);
    delete processes[result.pid];
  }
}

process.on('SIGCHLD', waitPids);

module.exports = function (name, path, callback) {
  binding.getContainer(name, path, function (err, container) {
    if (err) {
      return callback(err);
    }

    callback(null, new Container(container));
  });
};
