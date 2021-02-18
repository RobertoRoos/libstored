# libstored, distributed debuggable data stores.
# Copyright (C) 2020-2021  Jochem Rutgers
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import sys
import zmq
import logging
import time
import queue
import threading

from .zmq_server import ZmqServer
from . import protocol

class InfiniteStdoutBuffer:
    def __init__(self, stdout=sys.__stdout__, cleanup=None):
        self.stdout = stdout
        self._queue = queue.Queue()
        self._closed = False
        self._cleanup = cleanup
        self._thread = threading.Thread(target=self._worker)
        self._thread.daemon = True
        self._thread.start()

    def write(self, data):
        if self._closed:
            self._write(data)
        else:
            self._queue.put(data)

    def _write(self, data):
        # This may block.
        self.stdout.write(data)

    def flush(self):
        self._queue.join()

    def close(self):
        if self._closed:
            return

        self.flush()
        self._closed = True
        # Force wakeup
        self._queue.put('')
        self._thread.join()
        self._queue = None

        self.stdout.flush()

        if self._cleanup != None:
            self._cleanup()

    def __del__(self):
        self.close()

    def _worker(self):
        while not self._closed:
            self._write(self._queue.get())
            self._queue.task_done()

def resetStdout(old_stdout):
    sys.stdout = old_stdout

def setInfiniteStdout():
    if isinstance(sys.stdout, InfiniteStdoutBuffer):
        return
    sys.stdout.flush()
    old_stdout = sys.stdout
    sys.stdout = InfiniteStdoutBuffer(old_stdout, lambda: resetStdout(old_stdout))

class Stream2Zmq(protocol.ProtocolLayer):
    """A generic out-of-band frame grabber for ASCII streams."""

    default_port = ZmqServer.default_port

    def __init__(self, stack='ascii,term', listen='*', port=default_port, timeout_s=1):
        super().__init__()
        self.logger = logging.getLogger(__name__)
        self._stack_def = f'zmq={listen}:{port},' + stack
        self._timeout_s = timeout_s
        self._zmq = None
        setInfiniteStdout()
        self.reset()

    def reset(self):
        self._stack = protocol.buildStack(self._stack_def)
        for l in self._stack:
            if isinstance(l, protocol.TerminalLayer):
                l.fdout = self.stdout
        self.wrap(self._stack)
        self._zmq = None

    def encode(self, data):
        self.logger.debug('encode ' + str(bytes(data)))
        super().encode(data)

    def decode(self, data):
        self.logger.debug('decode ' + str(bytes(data)))
        super().decode(data)

    def timeout(self):
        self._stack.timeout()

    def stdout(self, data):
        sys.stdout.write(data.decode(errors="replace"))

    def isWaiting(self):
        return self.zmq.isWaiting()

    def poll(self, timeout_s = None):
        self.logger.debug('poll')

        if self.isWaiting():
            if timeout_s == None:
                timeout_s = self._timeout_s
            remaining = self.zmq.lastActivity() + self._timeout_s - time.time()
            if remaining <= 0:
                self.timeout()
            else:
                timeout_s = min(timeout_s, remaining)

        return self.zmq.poll(timeout_s)

    def recvAll(self, socket, f):
        try:
            while True:
                # Drain socket.
                f(socket.recv(flags=zmq.NOBLOCK))
        except zmq.ZMQError as e:
            if e.errno == zmq.EAGAIN:
                pass
            else:
                raise

    def registerStream(self, stream, f=True):
        return self.zmq.registerStream(stream, f)

    @property
    def zmq(self):
        if self._zmq == None:
            # Cache the zmq layer.
            self._zmq = next(iter(self._stack))
        return self._zmq

    def close(self):
        self.zmq.close()
