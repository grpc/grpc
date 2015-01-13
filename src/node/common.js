var _ = require('highland');

/**
 * When the given stream finishes without error, call the callback once. This
 * will not be called until something begins to consume the stream.
 * @param {function} callback The callback to call at stream end
 * @param {stream} source The stream to watch
 * @return {stream} The stream with the callback attached
 */
function onSuccessfulStreamEnd(callback, source) {
  var error = false;
  return source.consume(function(err, x, push, next) {
    if (x === _.nil) {
      if (!error) {
        callback();
      }
      push(null, x);
    } else if (err) {
      error = true;
      push(err);
      next();
    } else {
      push(err, x);
      next();
    }
  });
}

exports.onSuccessfulStreamEnd = onSuccessfulStreamEnd;
