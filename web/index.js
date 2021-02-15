const express = require('express');
const debug = require('debug')('server');
const cors = require('cors');
const child_process = require('child_process');
var stream   = require('stream');
const email_transporter = require('./email_transporter.js');

var app = express();

app.use(express.static('static'));
app.use(cors());
app.use(express.urlencoded({
  extended: true
}))

function send_email(to, subject, body) {
  debug('New email.');
  debug('To: ' + to);
  debug('Body: ' + body);

  var mailOptions = {
    from: '"John Doe" <john@ahv-defender.com>',
    to: to,
    subject: subject,
    text: body
  };
  email_transporter.sendMail(mailOptions, (error, info) => {
    if (error) {
      debug('Email could not be sent: ' + error);
    } else {
      debug('Check your inbox!');
    }
  });
}

function check_email(text, cb) {
    var child = child_process.execFile(
        "./email-analyzer", ['thorough', 'localhost:12000'],
        function (err, stdout, stderr) {
          if (err || stdout) {
            cb(true);
          } else {
            cb(false);
          }
        });
    var stdin_stream = new stream.Readable();
    stdin_stream.push(text);
    stdin_stream.push(null);
    stdin_stream.pipe(child.stdin);
}

app.post('/send_email', function (req, res, next) {
  if (!req.body.to || !req.body.sub || !req.body.eml) {
    debug('Form incorrectly filled.');
    res.redirect('/');
  } else {
    let all_text = req.body.to + ' ' + req.body.sub + ' ' + req.body.eml;
    check_email(all_text, function(filter) {
      if (filter) {
        res.redirect('/email_filtered.html');
      } else {
        send_email(req.body.to, req.body.sub, req.body.eml);
        res.redirect('/email_sent.html');
      }
    });
  }
});

app.use(function (err, req, res, next) {
  debug(err.stack);
  res.status(500).send('Internal server error.');
});

const port = 7835;
app.listen(port, () => console.log('Server started on port ' + port));
