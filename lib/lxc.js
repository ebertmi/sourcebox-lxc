'use strict';

var fs = require('fs');

var constants = process.binding('constants');
var pathModule = require('path');

var _ = require('lodash');
var concat = require('concat-stream');

var fsUtils = require('./fsUtils');
var binding = require('bindings')('lxc.node');
var AttachedProcess = require('./attach.js');
var common = require('./common.js');

/**
 * @class
 * @protected
 */
function Container(name, container) {
  this._name = name;
  this._container = container;
  this._container.owner = this;
}

Container.prototype.create = function (template, backingstore, args, callback) {
  if (!_.isString(template)) {
    throw new TypeError('template argument must be a string');
  }

  if (!_.isString(backingstore)) {
    throw new TypeError('backingstore argument must be a string');
  }

  if (_.isFunction(args)) {
    callback = args;
    args = [];
  }

  this._container.create(template, backingstore, args, callback);
};

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

Container.prototype.clone = function (name, options, callback) {
  if (!_.isPlainObject(options)) {
    if (!_.isFunction(options)) {
      throw new TypeError('options argument must be an object');
    }

    callback = options;
    options = {};
  }

  options = _.defaults({}, options, {
    snapshot: true,
    keepname: false,
    keepmac: false
  });

  this._container.clone(name, options, function (err, container) {
    if (err) {
      return callback(err);
    }

    callback(null, new Container(name, container));
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

  // node.js child_process compatibility
  if (options && options.streams === undefined &&
      options.stdio && options.stdio !== 'pipe') {
    if (_.isArray(options.stdio) &&
        options.stdio.length >= 3 && _.every(options.stdio, _.matches('pipe'))) {
      options.streams = options.stdio.length - 3;
    } else {
      throw new TypeError('Only \'pipe\' stdio configurations are supported');
    }
  }

  options = _.defaults({}, options, {
    cwd: '/',
    env: {},
    term: false,
    cgroup: true,
    streams: 0
  });

  options.env = _.map(options.env, function (value, key) {
    if (value === null || value === undefined) {
      return;
    }

    return key + '=' + value;
  });

  if (_.isArray(options.namespaces)) {
    options.namespaces = options.namespaces.map(function (ns) {
      return ns.toLowerCase();
    });
  }

  return this._container.attach(AttachedProcess, command, args, options);
};

function configFile(container, save, file, callback) {
  if (_.isFunction(file)) {
    callback = file;
    file = '';
  }

  container.configFile(file, save, callback);
}

Container.prototype.loadConfig = function (file, callback) {
  configFile(this._container, false, file, callback);
};

Container.prototype.saveConfig = function (file, callback) {
  configFile(this._container, true, file, callback);
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

  var setKey = function (value) {
    try {
      this.clearConfigItem(key);
    } catch (e) {}

    if (_.isArray(value)) {
      value.forEach(function (value) {
        this._container.setConfigItem(key, value);
      }, this);
    } else {
      this._container.setConfigItem(key, value);
    }
  }.bind(this);

  try {
    setKey(value);
  } catch (err) {
    setKey(oldValue);
    throw err;
  }
};

/**
 * Append `value` to `key`, assuming `key` is a list.
 * If `key` isn't a list, `value` will be set as the value of `key`.
 */
Container.prototype.appendConfigItem = function (key, value) {
  key = normalizeConfigKey(key);
  this._container.setConfigItem(key, value);
};

Container.prototype.clearConfigItem = function (key) {
  key = normalizeConfigKey(key);
  this._container.clearConfigItem(key);
};

Container.prototype.getRunningConfigItem = function (key) {
  return this._container.getRunningConfigItem(key);
};

/**
 * Get the value of a cgroup subsystem of a running container.
 *
 * @param {String} key Name of the subsystem
 */
Container.prototype.getCgroupItem = function (key) {
  var value = this._container.getCgroupItem(key);
  return _.trimRight(value, '\n');
};

/**
 * Set the value of a cgroup subsystem of a running container.
 *
 * @param {String} key Name of the subsystem
 * @param {String|Number} value Value to set
 */
Container.prototype.setCgroupItem = function (key, value) {
  this._container.setCgroupItem(key, value);
};

/**
 * Opens a file descriptor inside the container that can than be used with the
 * require('fs') methods that take a fd.
 *
 * This method is necessary because opening a file from the outside is
 * vulnerable to symlink attacks.
 */
Container.prototype.openFile = function (path, flags, options, callback) {
  if (!_.isPlainObject(options)) {
    if (!_.isFunction(options)) {
      throw new TypeError('options argument must be an object');
    }

    callback = options;
    options = {};
  }

  options = _.defaults({}, options, {
    mode: 438, // = 0666, will be changed by umask (probably to 0644)
    uid: 0,
    gid: 0
  });

  if (_.isString(options.mode)) {
    options.mode = parseInt(options.mode, 8);
  }

  flags = fsUtils.stringToFlags(flags);
  callback = _.once(callback);

  var helper = this._container.openFile(AttachedProcess, path, flags,
                                        options.mode, options.uid, options.gid);

  helper.on('error', callback);
  helper.on('exit', function (code, signal) {
    if (signal) {
      callback(new Error('FDHelper was killed: ' + signal));
    }
  });

  helper.stdout.pipe(concat(function (stdout) {
    var fdNumber = parseInt(stdout);

    if (isNaN(fdNumber)) {
      return;
    }

    var procPath = pathModule.join('/proc', helper.pid.toString(),
                                   'fd', fdNumber.toString());

    fs.open(procPath, flags & ~constants.O_EXCL, function (err, fd) {
      helper.stdin.end();
      callback(err, fd);
    });

  }));

  helper.stderr.pipe(concat(function (stderr) {
    var errno = parseInt(stderr);

    if (isNaN(errno)) {
      return;
    }

    var err = common.errnoException(errno, 'open', path);
    callback(err);
  }));
};

function getContainer(name, options, callback) {
  if (_.isFunction(options)) {
    callback = options;
    options = {};
  }

  options = _.defaults({}, options, {
    path: '',
    defined: true
  });

  binding.getContainer(name, options.path, options.defined, function (err, container) {
    if (err) {
      return callback(err);
    }

    callback(null, new Container(name, container));
  });
}

module.exports = exports = getContainer;
exports.getContainer = getContainer;
exports.version = binding.version;
exports._Container = Container; // export container for auto promisification
