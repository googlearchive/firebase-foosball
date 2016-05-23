# Copyright 2016 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
#Unless required by applicable law or agreed to in writing, software
#distributed under the License is distributed on an "AS IS" BASIS,
#WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#See the License for the specific language governing permissions and
#limitations under the License.

"""
The Yun's Atheros-side client to the firebase streaming API.

The script is responsible for streaming from:

  1. The table's current game.
  2. The current game's data (scores).
  3. The Yun bridgeclient.

And sending to:

  1. The Yun bridgeclient.


There are a few constraints here that prevent this from being a nice script.
Most importantly we have no space (flash or ram) for dependencies like requests
or an SSE client which depend on requests anyway. Still, it's nice to be
reminded of the early 2000s when Python was still fun.
"""

import json
import logging
import select
import socket
import sys
import time
import urllib2
import uuid


# Now try to load the bridge library which is installed in the following bizarre
# location on the Yun. If they had only thought to add "site-packages" to
# that path we wouldn't need this.
sys.path.insert(0, '/usr/lib/python2.7/bridge/') 
try:
  from bridgeclient import BridgeClient
  bridge_available = True
except ImportError:
  # Use the fake bridge client - we are developing off the Yun.
  bridge_available = False
  class BridgeClient(object):
    """A fake fake."""


class FakeBridgeClient(object):
  """A bridge that can be run off a yun, for testing."""

  def begin(self):
    pass

  def mailbox(self, msg):
    print 'SENDING', msg

  def mailbox_read(self, timeout=0):
    pass

  def socket_open(self):
    pass

  def close(self):
    pass


class CustomBridgeClient(BridgeClient):
  """Customized bridge client to add message receiving functionality.

  Another strange decision to omit mailbox_read from the bridge client, since the
  equivalent API to send messages on the Arduino side *is* implemented.
  """

  def mailbox_read(self, timeout=0):
    """Read a single mailbox message."""
    data = self.socket_open()
    r = self.wait_response(data, timeout)
    m = None
    if r is not None:
      try:
        m = r['data']
      except:
        pass
    self.socket_close()
    return m


def create_bridge():
  """Creates the correct bridge if we are on or off the Yun."""
  if bridge_available:
    return CustomBridgeClient()
  else:
    return FakeBridgeClient()

bridge = create_bridge()



DATA_NAME = "https://functions-prerelease-11-bb35b.firebaseio.com"
DATA_POST = json.dumps({'.sv': 'timestamp'})
DATA_AUTH = ('?auth=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJleHAiOj'
             'E0NjU2NjMwMDksInYiOjAsImQiOnsidWlkIjoiYmFuYW5hIn0sIml'
             'hdCI6MTQ2MzA3MTAwOX0.qEaQ0mSFd48a_NiUIX53OI6PuLKOOFRvJ'
             '9akcBMEKH4')

TABLE_INTERVAL = 60
CONFIG_INTERVAL = 10
GAME_INTERVAL = 30

LOG_FORMAT = '%(asctime)s:%(levelname)s:%(message)s'
DATE_FORMAT = '%a:%I:%M:%S'

logging.basicConfig(level=logging.DEBUG, format=LOG_FORMAT, datefmt=DATE_FORMAT)

def log(msg):
  return logging.info(msg)

class Timer(object):

  def __init__(self):
    self.reset()

  def reset(self):
    self.start = time.time()

  def past(self, seconds):
    return (time.time() - self.start) > seconds


class Client(object):

  def __init__(self):
    ##### emulating another board
    self.board_id = 'B4218AF83536'
    log('my real mac>%s' % self.get_mac())
    self.board_id = self.get_mac()
    self.game_id = None
    self.score = [0, 0]
    self.table_conn = None
    self.game_conn = None
    self.table_timer = None
    self.game_timer = None
    self.config_timer = None
    self.can_loop = True

  def start(self):
    """Starts the table and blocks."""
    self.send_config()
    self.config_timer = Timer()
    self.listen_table()
    self.loop()
    
  def loop(self):
    """Main loop."""
    while self.can_loop:
      if self.table_conn is None:
        self.listen_table()
      self.check_timers()
      self.receive_bridge()
      self.receive_http()
      time.sleep(0.1)

  def open_stream(self, url):
    """Opens a Firebase url as an event stream."""
    req = urllib2.Request(url)
    req.add_header('Accept', 'text/event-stream')
    opener = urllib2.build_opener()
    try:
      resp = opener.open(req, timeout=3)
    except urllib2.URLError:
      return
    # Unbuffered. Undocumented superfix.
    resp.fp._rbufsize = -1
    return resp

  def listen_table(self):
    """Listen to the current table stream for new games."""
    self.stop_listen_table()
    url = '%s/tables/%s/current_game.json%s' % (
        DATA_NAME, self.board_id, DATA_AUTH)
    self.table_conn = self.open_stream(url)
    self.table_timer = Timer()

  def listen_game(self):
    """Listen to the current game."""
    self.stop_listen_game()
    url = '%s%s.json%s' % (DATA_NAME, self.game_id, DATA_AUTH)
    self.game_conn = self.open_stream(url)
    self.game_timer = Timer()

  def stop_listen_table(self):
    if self.table_conn is not None:
      self.table_conn.close()

  def stop_listen_game(self):
    if self.game_conn is not None:
      self.game_conn.close()

  def start_bridge(self):
    """Start the bridge, and return it."""
    try:
      bridge.begin()
      return True
    except socket.error:
      return False

  def check_timers(self):
    """Check the timers, and if so restart ourselves."""
    if self.table_timer.past(TABLE_INTERVAL):
      self.listen_table()
    if self.game_timer is not None and self.game_timer.past(5):
      self.listen_game()
    if self.config_timer.past(CONFIG_INTERVAL):
      self.send_config()
      self.config_timer = Timer()

  def receive_bridge(self):
    """Reads from the bridge, and dispatches accordingly"""
    if not self.start_bridge():
      return
    msg = bridge.mailbox_read();
    if msg:
      print msg
      self.send_button(msg.strip())

  def receive_http(self):
    """Reads from the available http sockets, and dispatches accordingly."""
    sel = []
    if self.game_conn is not None:
      sel.append(self.game_conn)
    if self.table_conn is not None:
      sel.append(self.table_conn)
    se = select.select(sel, [], sel, 0.1)
    ss = se[0]
    for resp in ss:
      first = resp.readline().strip()
      if first == '':
        first = resp.readline().strip()
      second = resp.readline().strip()
      if first != "event: put":
        continue
      data = json.loads(second[6:])
      if resp is self.table_conn:
        self.game_received(data)
      elif resp is self.game_conn:
        self.score_received(data)

  def send_button(self, button_name):
    """Send a named button to Firebase after attaching the board ID."""
    button_id = '%s-%s' % (button_name, self.board_id)
    url = '%s/switches/%s/hits.json%s' % (DATA_NAME, button_id, DATA_AUTH)
    log('sending button>%s' % url)
    req = urllib2.Request(url, DATA_POST, {'Content-Type': 'application/json'})
    resp = urllib2.urlopen(req)
    content = resp.read()
    log('sending button<%s' % content)
    resp.close()

  def game_received(self, m):
    """Receives a message over the current table stream."""
    game = m['data']
    if game != self.game_id:
      self.game_id = game
      self.listen_game()

  def score_received(self, m):
    """Receives a score ove the current game stream."""
    path = m['path']
    data = m['data']
    if path == "/":
      self.score[0] = data.get('team_1_score', self.score[0])
      self.score[1] = data.get('team_2_score', self.score[1])
    elif path == "/team_1_score":
      self.score[0] = data
    elif path == "/team_2_score":
      self.score[1] = data
    self.send_score()

  def send_score(self):
    """Sends the score to the Arduino over the mailbox."""
    score = 'sco:%s %s' % tuple(self.score)
    if not self.start_bridge():
      return
    bridge.mailbox(score)
    
  def send_config(self):
    """Sends the config to the Arduino over the mailbox."""
    mac = 'mac:%s' % self.board_id
    eip = 'eip:%s' % self.get_ip()
    if not self.start_bridge():
      return
    bridge.mailbox(mac)
    bridge.mailbox(eip)

  def get_mac(self):
    return '%012X' % uuid.getnode()

  def get_ip(self):
    try:
      s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
      s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
      s.connect(('<broadcast>', 0))
      return s.getsockname()[0]
    except socket.error:
      return 'no idea'

if __name__ == '__main__':
  c = Client()
  c.start()
