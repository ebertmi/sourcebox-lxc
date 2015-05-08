var events = require('events');
var net = require('net');
var util = require('util');

var _ = require('lodash');
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

    if (result.exitCode === 128) {
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
    attachedProcess.emit('close', attachedProcess.exitCode,
                         attachedProcess.signalCode);
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
  binding.resize(this._handle.fd, cols, rows);
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
    throw new Error('Process not attached');
  }

  try {
    return process.kill(this.pid, signal);
  } catch (err) {
    if (err.code === undefined || err.code === 'EINVAL' ||
        err.code === 'ENOSYS') {
      // unknown or unsupported signal
      throw err;
    }

    if (err.code !== 'ESRCH') {
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

Container.prototype.start = function (command, args, callback) {
  if (!_.isString(command)) {
    throw new TypeError('command argument must be a string');
  }

  if (!_.isArray(args)) {
    if (!_.isFunction(args)) {
      throw new TypeError('args argument must be an array');
    }

    callback = args;
    args = [];
  }

  this._container.start([].concat(command, args), callback);
};

Container.prototype.stop = function (callback) {
  this._container.stop(callback);
};

Container.prototype.destroy = function (callback) {
  this._container.destroy(callback);
};

Container.prototype.state = function () {
  return this._container.state();
};

Container.prototype.clone = function (name, options, callback) {
  var defaults = {
    snapshot: true,
    keepname: false,
    keepmac: false
  };

  if (!_.isObject(options)) {
    if (!_.isFunction(options)) {
      throw new TypeError('options argument must be an object');
    }

    callback = options;
    options = {};
  }

  options = _.assign(defaults, options);

  this._container.clone(name, options, function (err, container) {
    if (err) {
      return callback(err);
    }

    callback(null, new Container(container));
  });
};

/**
 * @returns {AttachedProcess}
 */
Container.prototype.attach = function (command, args, options) {
  if (!_.isArray(args)) {
    if (args !== undefined && !_.isObject(args)) {
      throw new TypeError('args argument must be an array');
    }

    options = args;
    args = [];
  }

  if (options !== undefined && !_.isObject(options)) {
    throw new TypeError('options argument must be an object');
  }

  var defaults = {
    cwd: '/',
    env: {},
    term: false,
    fds: 0
  };

  options = _.assign(defaults, options);

  var envPairs = [];

  for (var key in options.env) {
    envPairs.push(key + '=' + options.env[key]);
  }

  options.env = envPairs;

  var attachedProcess;

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
  attachedProcess = new AttachedProcess(command, fds, options.term, this);

  return attachedProcess;
};

Container.prototype.getKeys = function () {
  var keys = this._container.getKeys();

  if (_.endsWith(keys, '\n')) {
    keys = keys.split('\n');
    keys.pop();
  }

  return keys;
};

function normalizeConfigKey(key) {
  var prefix = 'lxc.';
  return _.startsWith(key, prefix) ? key : prefix + key;
}

Container.prototype.getConfigItem = function (key) {
  key = normalizeConfigKey(key);
  var value = this._container.getConfigItem(key);

  if (_.endsWith(value, '\n')) {
    value = value.split('\n');
    value.pop();
  }

  return value;
};

Container.prototype.setConfigItem = function (key, value) {
  key = normalizeConfigKey(key);
  var oldValue = this.getConfigItem(key);

  var setKey = (function (value) {
    this.clearConfigItem(key);

    if (_.isArray(value)) {
      return value.every(function (value) {
        return this._container.setConfigItem(key, value);
      }, this);
    } else {
      return this._container.setConfigItem(key, value);
    }
  }).bind(this);

  if (!setKey(value)) {
    setKey(oldValue);
    return false;
  }

  return true;
};

/**
 * Append `value` to `key`, assuming `key` is a list.
 * If `key` isn't a list, `value` will be set as the value of `key`.
 */
Container.prototype.appendConfigItem = function (key, value) {
  key = normalizeConfigKey(key);
  return this._container.setConfigItem(key, value);
};

Container.prototype.clearConfigItem = function (key) {
  key = normalizeConfigKey(key);
  return this._container.clearConfigItem(key);
};

module.exports = function (name, path, callback) {
  binding.getContainer(name, path, function (err, container) {
    if (err) {
      return callback(err);
    }

    callback(null, new Container(container));
  });
};
