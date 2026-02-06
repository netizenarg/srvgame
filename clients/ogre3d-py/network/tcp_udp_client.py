"""
TCP/UDP client for connecting to C++ game server
"""

import socket
import json
import threading
import time
import logging
import struct
from enum import Enum
from queue import Queue, Empty
import msgpack
import zlib

from .protocol import MessageType, GameMessage, MessageBuilder, MessageParser

logger = logging.getLogger(__name__)


class TcpUdpClient:
    """TCP/UDP client for game server communication"""

    def __init__(self, host, port, game_state, use_udp=False):
        self.host = host
        self.port = port
        self.game_state = game_state
        self.use_udp = use_udp

        self.socket = None
        self.connected = False
        self.session_id = 0
        self.player_id = 0

        # Message queues
        self.outgoing_queue = Queue()
        self.incoming_queue = Queue()

        # Thread control
        self.running = False
        self.receive_thread = None
        self.send_thread = None

        # Connection stats
        self.latency = 0
        self.packets_sent = 0
        self.packets_received = 0
        self.last_ping_time = 0
        self.last_pong_time = 0

        # Binary protocol support
        self.use_binary_protocol = False
        self.compression_enabled = True

        # Message handlers
        self.handlers = {
            MessageType.WORLD_DATA: self.handle_world_data,
            MessageType.ENTITY_UPDATE: self.handle_entity_update,
            MessageType.CHAT_BROADCAST: self.handle_chat,
            MessageType.ERROR_MESSAGE: self.handle_error,
            MessageType.LOGIN_RESPONSE: self.handle_login_response,
            MessageType.PONG: self.handle_pong,
            MessageType.QUEST_UPDATE: self.handle_quest_update,
            MessageType.INVENTORY_UPDATE: self.handle_inventory_update,
            MessageType.TRADE_UPDATE: self.handle_trade_update,
            MessageType.COMBAT_RESULT: self.handle_combat_result
        }

    def connect(self):
        """Connect to game server"""
        try:
            if self.use_udp:
                self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.socket.settimeout(1.0)
            else:
                self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.socket.settimeout(5.0)
                self.socket.connect((self.host, self.port))
                self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            self.running = True
            self.connected = True

            # Start network threads
            self.receive_thread = threading.Thread(target=self.receive_loop, daemon=True)
            self.send_thread = threading.Thread(target=self.send_loop, daemon=True)

            self.receive_thread.start()
            self.send_thread.start()

            # Send login message
            self.login()

            logger.info(f"Connected to {self.host}:{self.port} via {'UDP' if self.use_udp else 'TCP'}")
            return True

        except Exception as e:
            logger.error(f"Connection failed: {e}")
            self.connected = False
            return False

    def disconnect(self):
        """Disconnect from server"""
        self.running = False
        self.connected = False

        if self.socket:
            try:
                self.send_logout()
            except Exception as e:
                logger.error(f'{e}')
            else:
                try:
                    self.socket.close()
                except Exception as e:
                    logger.error(f'{e}')

        if self.receive_thread:
            self.receive_thread.join(timeout=1.0)
        if self.send_thread:
            self.send_thread.join(timeout=1.0)

        logger.info("Disconnected from server")

    def receive_loop(self):
        """Receive messages from server"""
        buffer = b''

        while self.running and self.connected:
            try:
                if self.use_udp:
                    data, _ = self.socket.recvfrom(65535)
                else:
                    data = self.socket.recv(4096)

                if not data:
                    logger.warning("Connection lost")
                    self.connected = False
                    break

                buffer += data

                # Process complete messages
                while b'\n' in buffer:
                    message, buffer = buffer.split(b'\n', 1)

                    # Decompress if needed
                    try:
                        if message.startswith(b'x'):
                            message = zlib.decompress(message)

                        self.process_incoming_message(message)
                    except Exception as e:
                        logger.error(f"Message processing error: {e}")

                self.packets_received += 1

            except socket.timeout:
                continue
            except ConnectionResetError:
                logger.error("Connection reset by peer")
                self.connected = False
                break
            except Exception as e:
                logger.error(f"Receive error: {e}")
                self.connected = False
                break

    def process_incoming_message(self, message):
        """Process incoming message from server"""
        try:
            if self.use_binary_protocol:
                # Parse binary message
                parsed_message = GameMessage.deserialize(message)
                if parsed_message:
                    self.process_binary_message(parsed_message)
                else:
                    logger.warning("Failed to parse binary message")
            else:
                # Parse JSON message
                message_str = message.decode('utf-8')
                message_data = json.loads(message_str)
                self.process_json_message(message_data)
                
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON: {e}")
        except UnicodeDecodeError:
            # Try binary protocol
            try:
                parsed_message = GameMessage.deserialize(message)
                if parsed_message:
                    self.process_binary_message(parsed_message)
                else:
                    logger.warning("Failed to parse message as either JSON or binary")
            except:
                logger.error("Message could not be parsed as JSON or binary")

    def process_json_message(self, message_data):
        """Process JSON message"""
        msg_type = message_data.get('type')
        data = message_data.get('data', {})
        
        if msg_type == 'ping':
            self.send_pong(data.get('timestamp'))
        elif msg_type == 'pong':
            self.handle_pong_response(data.get('timestamp'))
        elif msg_type == 'error':
            logger.error(f"Server error: {data.get('message')}")
        elif msg_type == 'login_response':
            if data.get('success'):
                self.session_id = data.get('session_id', 0)
                self.player_id = data.get('player_id', 0)
        elif msg_type == 'world_chunk':
            self.incoming_queue.put({
                'type': 'world_chunk',
                'data': data
            })
        elif msg_type == 'entity_update':
            self.incoming_queue.put({
                'type': 'entity_update',
                'data': data
            })
        elif msg_type == 'chat':
            self.incoming_queue.put({
                'type': 'chat',
                'data': data
            })
        elif msg_type == 'collision':
            self.incoming_queue.put({
                'type': 'collision',
                'data': data
            })
        elif msg_type == 'npc_interaction':
            self.incoming_queue.put({
                'type': 'npc_interaction',
                'data': data
            })

    def process_binary_message(self, message):
        """Process binary message"""
        if message.msg_type in self.handlers:
            self.handlers[message.msg_type](message.data)
        else:
            logger.warning(f"No handler for binary message type: {message.msg_type}")

    def send_loop(self):
        """Send messages to server"""
        while self.running and self.connected:
            try:
                message = self.outgoing_queue.get(timeout=0.1)

                if self.use_udp:
                    self.socket.sendto(message + b'\n', (self.host, self.port))
                else:
                    self.socket.sendall(message + b'\n')

                self.packets_sent += 1

            except Empty:
                continue
            except BrokenPipeError:
                logger.error("Broken pipe - connection lost")
                self.connected = False
                break
            except Exception as e:
                logger.error(f"Send error: {e}")
                self.connected = False
                break

    def send_message(self, msg_type, data):
        """Send message to server"""
        if self.use_binary_protocol:
            self.send_binary_message(msg_type, data)
        else:
            self.send_json_message(msg_type, data)

    def send_json_message(self, msg_type, data):
        """Send JSON message to server"""
        message = {
            'type': msg_type.value if hasattr(msg_type, 'value') else msg_type,
            'data': data,
            'timestamp': time.time(),
            'session_id': self.session_id,
            'player_id': self.player_id
        }

        # Compress if large
        message_str = json.dumps(message)
        if len(message_str) > 1024 and self.compression_enabled:
            compressed = zlib.compress(message_str.encode())
            self.outgoing_queue.put(compressed)
        else:
            self.outgoing_queue.put(message_str.encode())

    def send_binary_message(self, msg_type, data):
        """Send binary message to server"""
        if isinstance(msg_type, MessageType):
            message = GameMessage(msg_type, data, self.session_id)
        else:
            # Convert string/other type to MessageType
            try:
                msg_type_enum = MessageType(msg_type)
                message = GameMessage(msg_type_enum, data, self.session_id)
            except:
                logger.error(f"Invalid message type: {msg_type}")
                return

        binary_data = message.serialize()
        
        if len(binary_data) > 1024 and self.compression_enabled:
            compressed = zlib.compress(binary_data)
            self.outgoing_queue.put(compressed)
        else:
            self.outgoing_queue.put(binary_data)

    # Message handlers for binary protocol
    def handle_world_data(self, data):
        """Handle binary world data"""
        parsed = MessageParser.parse_world_data(data)
        self.incoming_queue.put({
            'type': 'world_chunk',
            'data': parsed
        })

    def handle_entity_update(self, data):
        """Handle binary entity update"""
        parsed = MessageParser.parse_entity_update(data)
        self.incoming_queue.put({
            'type': 'entity_update',
            'data': parsed
        })

    def handle_chat(self, data):
        """Handle binary chat message"""
        parsed = MessageParser.parse_chat(data)
        self.incoming_queue.put({
            'type': 'chat',
            'data': parsed
        })

    def handle_error(self, data):
        """Handle binary error message"""
        parsed = MessageParser.parse_error(data)
        logger.error(f"Server error: {parsed.get('message')}")

    def handle_login_response(self, data):
        """Handle binary login response"""
        parsed = MessageParser.parse_login_response(data)
        if parsed.get('success'):
            self.session_id = parsed.get('session_id', 0)
            self.player_id = parsed.get('player_id', 0)

    def handle_pong(self, data):
        """Handle binary pong response"""
        self.handle_pong_response(data.get('timestamp', 0))

    def handle_quest_update(self, data):
        """Handle binary quest update"""
        self.incoming_queue.put({
            'type': 'quest_update',
            'data': data
        })

    def handle_inventory_update(self, data):
        """Handle binary inventory update"""
        self.incoming_queue.put({
            'type': 'inventory_update',
            'data': data
        })

    def handle_trade_update(self, data):
        """Handle binary trade update"""
        self.incoming_queue.put({
            'type': 'trade_update',
            'data': data
        })

    def handle_combat_result(self, data):
        """Handle binary combat result"""
        self.incoming_queue.put({
            'type': 'combat_result',
            'data': data
        })

    def process_messages(self):
        """Process messages in main thread"""
        processed = 0
        max_per_frame = 10  # Process up to 10 messages per frame
        
        while not self.incoming_queue.empty() and processed < max_per_frame:
            try:
                message = self.incoming_queue.get_nowait()
                self.game_state.apply_server_update(message)
                processed += 1
            except Empty:
                break
            except Exception as e:
                logger.error(f"Error processing message: {e}")

    # Client actions
    def login(self, player_name=None, auth_token=None):
        """Send login request"""
        login_data = {
            'player_name': player_name or 'Player',
            'auth_token': auth_token or '',
            'version': '1.0.0'
        }
        self.send_message(MessageType.LOGIN_REQUEST, login_data)

    def send_logout(self):
        """Send logout notification"""
        self.send_message(MessageType.LOGOUT_NOTIFY, {})

    def send_movement(self, position, rotation, velocity, flags=0):
        """Send player movement update"""
        movement_data = {
            'position': position,
            'rotation': rotation,
            'velocity': velocity,
            'flags': flags,
            'timestamp': time.time()
        }
        self.send_message(MessageType.MOVEMENT_UPDATE, movement_data)

    def send_chat(self, message, channel='global', target=''):
        """Send chat message"""
        chat_data = {
            'message': message,
            'channel': channel,
            'target': target,
            'timestamp': time.time()
        }
        self.send_message(MessageType.CHAT_MESSAGE, chat_data)

    def request_world_chunk(self, chunk_x, chunk_z, lod=0, include_entities=True):
        """Request world chunk from server"""
        chunk_data = {
            'chunk_x': chunk_x,
            'chunk_z': chunk_z,
            'lod': lod,
            'include_entities': include_entities
        }
        self.send_message(MessageType.WORLD_REQUEST, chunk_data)

    def send_entity_interaction(self, entity_id, interaction_type, data=None):
        """Interact with entity"""
        interaction_data = {
            'entity_id': entity_id,
            'interaction_type': interaction_type,
            'data': data or {}
        }
        self.send_message(MessageType.ENTITY_INTERACT, interaction_data)

    def send_combat_action(self, target_id, action_type, ability_id='', position=None):
        """Send combat action"""
        combat_data = {
            'target_id': target_id,
            'action_type': action_type,
            'ability_id': ability_id,
            'position': position or [0, 0, 0]
        }
        self.send_message(MessageType.COMBAT_ACTION, combat_data)

    def send_inventory_action(self, action_type, item_id, quantity=1, target_slot=None):
        """Send inventory action"""
        inventory_data = {
            'action_type': action_type,
            'item_id': item_id,
            'quantity': quantity,
            'target_slot': target_slot
        }
        self.send_message(MessageType.INVENTORY_ACTION, inventory_data)

    def send_quest_action(self, quest_id, action_type, data=None):
        """Send quest action"""
        quest_data = {
            'quest_id': quest_id,
            'action_type': action_type,
            'data': data or {}
        }
        self.send_message(MessageType.QUEST_ACTION, quest_data)

    def send_trade_request(self, target_player_id, items_offer, items_request):
        """Send trade request"""
        trade_data = {
            'target_player_id': target_player_id,
            'items_offer': items_offer,
            'items_request': items_request
        }
        self.send_message(MessageType.TRADE_REQUEST, trade_data)

    def ping(self):
        """Send ping to measure latency"""
        self.last_ping_time = time.time()
        ping_data = {'timestamp': self.last_ping_time}
        self.send_message(MessageType.PING, ping_data)

    def send_pong(self, timestamp):
        """Send pong response"""
        pong_data = {'timestamp': timestamp or time.time()}
        self.send_message(MessageType.PONG, pong_data)

    def handle_pong_response(self, timestamp):
        """Handle pong response from server"""
        if timestamp and self.last_ping_time:
            self.latency = (time.time() - timestamp) * 1000  # Convert to ms
            self.last_pong_time = time.time()

    def is_connected(self):
        """Check if connected to server"""
        return self.connected

    def get_stats(self):
        """Get connection statistics"""
        return {
            'connected': self.connected,
            'packets_sent': self.packets_sent,
            'packets_received': self.packets_received,
            'latency_ms': self.latency,
            'queue_size': self.outgoing_queue.qsize(),
            'session_id': self.session_id,
            'player_id': self.player_id,
            'protocol': 'binary' if self.use_binary_protocol else 'json',
            'transport': 'UDP' if self.use_udp else 'TCP'
        }

    def set_binary_mode(self, enabled=True):
        """Enable or disable binary protocol mode"""
        self.use_binary_protocol = enabled
        logger.info(f"Binary protocol mode: {enabled}")

    def set_compression(self, enabled=True):
        """Enable or disable compression"""
        self.compression_enabled = enabled
        logger.info(f"Compression: {enabled}")

    def get_queue_sizes(self):
        """Get current queue sizes"""
        return {
            'outgoing': self.outgoing_queue.qsize(),
            'incoming': self.incoming_queue.qsize()
        }

    def clear_queues(self):
        """Clear all message queues"""
        while not self.outgoing_queue.empty():
            try:
                self.outgoing_queue.get_nowait()
            except Empty:
                break
        
        while not self.incoming_queue.empty():
            try:
                self.incoming_queue.get_nowait()
            except Empty:
                break
        
        logger.info("Cleared all message queues")
