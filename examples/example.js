'use strict';

var lxc = require('..');

// get a container 
lxc('source.box', {
  path: '/home/trycoding/sb',
  defined: true // require container to exist
}, function (err, container) {
  if (err) {
    return console.error(err.message);
  }

  startContainer(container);
});


// start the container
function startContainer(container) {
  console.log('starting container');

  container.start('sleep', ['infinity'], function (err) {
    if (err) {
      return console.error(err.message);
    }

    runProcess(container);
  });
}

// once the container is running, we can attach processes
function runProcess(container) {
  console.log('running bash');

  // child behaves like a Node.js Process instance
  var child = container.attach('bash', {
    cwd: '/etc',
    env: {
      TERM: process.env.TERM,
      HOME: '/root'
    },

    // allocate a pseudo terminal for the process
    term: {
      columns: 80,
      rows: 24
    },

    // and add another duplex stream (FD 3 in the child process)
    streams: 1,

    // run as root
    uid: 0,

    // and as adm group
    gid: 4,

    // do not use cgroups (no limits)
    cgroup: false,
    namespaces: ['user', 'mount', 'net', 'uts']
  });

  // pipe child process stdout to node's stdout
  child.stdio[3].pipe(process.stdout);

  // write something to the child's stdin
  child.stdin.write('ls -la / >&3\n');

  // kill the process after 5s
  setTimeout(function () {
    child.kill('SIGKILL');
  }, 5000);

  child.on('error', function (err) {
    console.error('Process error:', err.message);

    stopContainer(container);
  });

  child.on('exit', function (status, signal) {
    console.log('Process terminated:', signal || status);

    stopContainer(container);
  });
}

function stopContainer(container) {
  console.log('stopping container');

  container.stop(function (err) {
    if (err) {
      console.error(err.message);
    }
  });
}
