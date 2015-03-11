var util = require('util');
var events = require('events');

var binding = require('bindings')('lxc.node');

/**
 * @class AttachedProcess
 */
function AttachedProcess(container, pid) {
  AttachedProcess.super_.call(this);

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
  process.kill(this.pid, signal);
};

/**
 * @class Container
 */
function Container(wrap) {
  this.wrap = wrap;
}

/**
 * @returns {AttachedProcess}
 */
Container.prototype.attach = function (cmd, args) {
  // hier vielleicht der native methode den AttachedProcess übergeben??
  // falls error trotzdem process returnen, dann in nexttick error event auslösen

  var result = this.wrap.attach(cmd, args);
  var attached = new AttachedProcess(this, result.pid);

  // vielleich das alles im AttachedProcess constructor
  if (result.error) {
    // FIXME this isnt working yet
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
    // gffs code und signal im prozess setzten, so dass sie immer verfügbar
    // sind, siehe child api -> close event
    processes[result.pid].emit('exit', result.code, result.signal);
    delete processes[result.pid];
  }
}

process.on('SIGCHLD', waitPids);

module.exports = function (name, path, cb) {
  binding.getContainer(name, path, function (err, wrap) {
    if (err) {
      return cb(err);
    }

    cb(null, new Container(wrap));
  });
};
