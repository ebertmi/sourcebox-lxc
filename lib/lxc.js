var _ = require('lodash');

var binding = require('bindings')('lxc.node');
var AttachedProcess = require('./attach.js');

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

  if (!_.isPlainObject(options)) {
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

  var defaults = {
    cwd: '/',
    env: {},
    term: false,
    streams: 0
  };

  options = _.assign(defaults, options);

  options.env = Object.keys(options.env).map(function (key) {
    return key + '=' + options.env[key];
  });

  return this._container.attach(AttachedProcess, command, args, options);
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
  return this._container.setCgroupItem(key, value);
};

module.exports = function (name, path, callback) {
  if (!_.isString(path)) {
    if (!_.isFunction(path)) {
      throw new TypeError("path argument must be a string");
    }

    callback = path;
    path = '';
  }

  binding.getContainer(name, path, function (err, container) {
    if (err) {
      return callback(err);
    }

    callback(null, new Container(container));
  });
};
