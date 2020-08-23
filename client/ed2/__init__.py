# vim:et

# libstored, a Store for Embedded Debugger.
# Copyright (C) 2020  Jochem Rutgers
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

##
# \defgroup libstored_client client
# \brief Python client interfaces to the embedded application.
# \ingroup libstored

from .zmq_server import ZmqServer
from .zmq_client import ZmqClient
from .stdio2zmq import Stdio2Zmq
from .serial2zmq import Serial2Zmq

__version__ = '0.0.1'

