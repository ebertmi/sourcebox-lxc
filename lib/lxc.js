var util = require('util');
var events = require('events');

var binding = require('bindings')('lxc.node');

/**
 * @class AttachedProcess
 */
function AttachedProcess(command, pid, container) {
  AttachedProcess.super_.call(this);

  this._command = command;
  this.container = container;
  this.pid = pid;
}

util.inherits(AttachedProcess, events.EventEmitter);

/**
 * Send a signal to the child process. If no argument is given, the process
 * will be sent `'SIGTERM'`. See `signal(7)` for a list of available signals.
 *
 * @param {String} [signal=SIGTERM] Signal to send
 */
AttachedProcess.prototype.kill = function (signal) {
  return process.kill(this.pid, signal);
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

  if (exitCode == 128) {
    this.emit('error', new Error('Failed to execute "' + this._command  + '"'));
  } else {
    this.emit('exit', this.exitCode, this.signalCode);
  }

  // TODO stream cleanup?
  // flush stdio? see child process api
};

/**
 * @class Container
 */
function Container(container) {
  this._container = container;
}

/**
 * @returns {AttachedProcess}
 */
Container.prototype.attach = function (command, args) {
  // hier vielleicht der native methode den AttachedProcess übergeben??
  // falls error trotzdem process returnen, dann in nexttick error event auslösen

  var result = this._container.attach(command, args);
  var attached = new AttachedProcess(command, result.pid, this);

  // vielleich das alles im AttachedProcess constructor
  if (result.error) {
    // this only happens when there is an lxc error, maybe just give up in this case
    process.nextTick(function () {
      attached.emit('error', new Error(result.error));
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

module.exports = function (name, path, cb) {
  binding.getContainer(name, path, function (err, container) {
    if (err) {
      return cb(err);
    }

    cb(null, new Container(container));
  });
};
