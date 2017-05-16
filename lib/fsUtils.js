var util = require('util');

// Check the nodejs version and try to create the constants
if (process.versions.node.startsWith("4.7")) {
  var constants = process.binding('constants');

  var O_APPEND = constants.O_APPEND || 0;
  var O_CREAT = constants.O_CREAT || 0;
  var O_EXCL = constants.O_EXCL || 0;
  var O_RDONLY = constants.O_RDONLY || 0;
  var O_RDWR = constants.O_RDWR || 0;
  var O_SYNC = constants.O_SYNC || 0;
  var O_TRUNC = constants.O_TRUNC || 0;
  var O_WRONLY = constants.O_WRONLY || 0;
} else if (process.versions.node.startsWith("7.") || process.binding('constants').fs != null) {
  var fsConstants = process.binding('constants').fs;

  var O_APPEND = fsConstants.O_APPEND;
  var O_CREAT = fsConstants.O_CREAT;
  var O_EXCL = fsConstants.O_EXCL;
  var O_RDONLY = fsConstants.O_RDONLY;
  var O_RDWR = fsConstants.O_RDWR;
  var O_SYNC = fsConstants.O_SYNC;
  var O_TRUNC = fsConstants.O_TRUNC;
  var O_WRONLY = fsConstants.O_WRONLY;

} else {
  console.error("Cannot access process.bindings('constants').fs - incompatible node version");
  process.exit();
}

/**
 * Convert a string to a fs flags
 * 
 * @param {String} flag - the flag as a string, e.g. 'r' or 'w+'
 * @returns {Number} - the integer flag(s)
 */
function stringToFlags(flag) {
  // Only mess with strings
  if (!util.isString(flag)) {
    return flag;
  }

  switch (flag) {
    case 'r' : return O_RDONLY;
    case 'rs' : // fall through
    case 'sr' : return O_RDONLY | O_SYNC;
    case 'r+' : return O_RDWR;
    case 'rs+' : // fall through
    case 'sr+' : return O_RDWR | O_SYNC;

    case 'w' : return O_TRUNC | O_CREAT | O_WRONLY;
    case 'wx' : // fall through
    case 'xw' : return O_TRUNC | O_CREAT | O_WRONLY | O_EXCL;

    case 'w+' : return O_TRUNC | O_CREAT | O_RDWR;
    case 'wx+': // fall through
    case 'xw+': return O_TRUNC | O_CREAT | O_RDWR | O_EXCL;

    case 'a' : return O_APPEND | O_CREAT | O_WRONLY;
    case 'ax' : // fall through
    case 'xa' : return O_APPEND | O_CREAT | O_WRONLY | O_EXCL;

    case 'a+' : return O_APPEND | O_CREAT | O_RDWR;
    case 'ax+': // fall through
    case 'xa+': return O_APPEND | O_CREAT | O_RDWR | O_EXCL;
  }

  throw new Error('Unknown file open flag: ' + flag);
}

module.exports = {
  stringToFlags
};
