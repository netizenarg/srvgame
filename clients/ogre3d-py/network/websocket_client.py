"""
WebSocket client for connecting to C++ game server with WebSocket protocol
"""

import asyncio
import json
import logging
import threading
import time
import zlib
import base64
from typing import Dict, Any, Optional, Callable
from enum import Enum
from queue import Queue, Empty

import websockets
from websockets.client import WebSocketClientProtocol
from websockets.exceptions import ConnectionClosed

from .protocol import MessageType, GameMessage, MessageBuilder, MessageParser

logger = logging.getLogger(__name__)


class WebSocketState(Enum):
    """WebSocket connection states"""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    RECONNECTING = "reconnecting"
    CLOSING = "closing"
    CLOSED = "closed"


class WebSocketGameClient:
    """WebSocket client for game server communication"""

    def __init__(self, config: Dict[str, Any], game_state):
        self.config = config
        self.game_state = game_state
        
        # WebSocket configuration
        ws_config = config.get('websocket', {})
        self.url = ws_config.get('url', 'ws://127.0.0.1:8080/ws')
        self.auto_reconnect = ws_config.get('auto_reconnect', True)
        self.reconnect_delay = ws_config.get('reconnect_delay', 5)
        self.max_reconnect_attempts = ws_config.get('max_reconnect_attempts', 10)
        self.ping_interval = ws_config.get('ping_interval', 30)
        self.ping_timeout = ws_config.get('ping_timeout', 10)
        self.protocols = ws_config.get('protocols', ['game-protocol-v1'])
        self.compression_enabled = ws_config.get('compression_enabled', True)
        
        # Connection state
        self.state = WebSocketState.DISCONNECTED
        self.ws: Optional[WebSocketClientProtocol] = None
        self.reconnect_attempts = 0
        self.last_ping_time = 0
        self.last_pong_time = 0
        self.latency = 0
        
        # Message queues
        self.outgoing_queue = Queue()
        self.incoming_queue = Queue()
        
        # Thread control
        self.running = False
        self.receive_thread = None
        self.send_thread = None
        self.ping_thread = None
        
        # Statistics
        self.messages_sent = 0
        self.messages_received = 0
        self.bytes_sent = 0
        self.bytes_received = 0
        self.reconnect_count = 0
        self.session_id = 0
        self.player_id = 0
        
        # Event handlers
        self.on_connect = None
        self.on_disconnect = None
        self.on_error = None
        self.on_message = None
        
        # Binary protocol support
        self.binary_mode = False
        
        # Rate limiting
        self.rate_limit_config = ws_config.get('rate_limit', {})
        self.last_message_time = 0
        self.message_count = 0
        self.rate_limit_reset_time = time.time()

    def set_event_handlers(self, on_connect=None, on_disconnect=None, 
                          on_error=None, on_message=None):
        """Set event handlers for WebSocket events"""
        self.on_connect = on_connect
        self.on_disconnect = on_disconnect
        self.on_error = on_error
        self.on_message = on_message

    def connect(self):
        """Connect to WebSocket server"""
        if self.state not in [WebSocketState.DISCONNECTED, WebSocketState.CLOSED]:
            logger.warning(f"Cannot connect from state: {self.state}")
            return False
        
        self.state = WebSocketState.CONNECTING
        self.running = True
        
        # Start threads
        self.receive_thread = threading.Thread(target=self._connect_and_receive, daemon=True)
        self.send_thread = threading.Thread(target=self._send_loop, daemon=True)
        self.ping_thread = threading.Thread(target=self._ping_loop, daemon=True)
        
        self.receive_thread.start()
        self.send_thread.start()
        self.ping_thread.start()
        
        return True

    async def _connect_async(self):
        """Async WebSocket connection"""
        try:
            logger.info(f"Connecting to WebSocket: {self.url}")
            
            # WebSocket connection options
            extra_headers = {
                'User-Agent': 'GameClient/1.0',
                'X-Game-Protocol': 'websocket-v1',
                'X-Client-Version': '1.0.0'
            }
            
            # Add session ID if we have one
            if self.session_id:
                extra_headers['X-Session-ID'] = str(self.session_id)
            
            # Connect to WebSocket
            self.ws = await websockets.connect(
                self.url,
                ping_interval=self.ping_interval,
                ping_timeout=self.ping_timeout,
                close_timeout=1,
                max_size=10 * 1024 * 1024,  # 10 MB
                compression='deflate' if self.compression_enabled else None,
                subprotocols=self.protocols,
                extra_headers=extra_headers
            )
            
            self.state = WebSocketState.CONNECTED
            self.reconnect_attempts = 0
            
            logger.info("WebSocket connected successfully")
            
            if self.on_connect:
                self.on_connect()
            
            return True
            
        except Exception as e:
            logger.error(f"WebSocket connection failed: {e}")
            self.state = WebSocketState.DISCONNECTED
            
            if self.on_error:
                self.on_error(str(e))
            
            return False

    def _connect_and_receive(self):
        """Main connection and receive loop"""
        asyncio.run(self._connection_loop())

    async def _connection_loop(self):
        """WebSocket connection loop with reconnection"""
        while self.running:
            try:
                # Connect to WebSocket
                connected = await self._connect_async()
                
                if not connected:
                    if self.auto_reconnect and self.reconnect_attempts < self.max_reconnect_attempts:
                        await self._handle_reconnect()
                        continue
                    else:
                        break
                
                # Connection successful, start receiving
                await self._receive_loop()
                
                # If we get here, connection was closed
                if self.ws and not self.ws.closed:
                    await self.ws.close()
                
                self.state = WebSocketState.DISCONNECTED
                
                if self.on_disconnect:
                    self.on_disconnect()
                
                # Auto-reconnect if enabled
                if self.auto_reconnect and self.running:
                    if self.reconnect_attempts < self.max_reconnect_attempts:
                        await self._handle_reconnect()
                    else:
                        logger.error(f"Max reconnection attempts ({self.max_reconnect_attempts}) reached")
                        break
                else:
                    break
                    
            except Exception as e:
                logger.error(f"Connection loop error: {e}")
                self.state = WebSocketState.DISCONNECTED
                
                if self.auto_reconnect and self.running:
                    await self._handle_reconnect()
                else:
                    break

    async def _handle_reconnect(self):
        """Handle reconnection with exponential backoff"""
        self.reconnect_attempts += 1
        self.reconnect_count += 1
        
        # Exponential backoff with jitter
        delay = min(self.reconnect_delay * (2 ** (self.reconnect_attempts - 1)), 300)
        jitter = delay * 0.1
        actual_delay = delay + (jitter * (0.5 - (hash(str(time.time())) % 1000) / 2000))
        
        logger.info(f"Reconnecting in {actual_delay:.1f}s (attempt {self.reconnect_attempts})")
        
        self.state = WebSocketState.RECONNECTING
        
        await asyncio.sleep(actual_delay)

    async def _receive_loop(self):
        """Receive messages from WebSocket server"""
        try:
            async for message in self.ws:
                if not self.running:
                    break
                    
                await self._process_incoming_message(message)
                
        except ConnectionClosed as e:
            logger.info(f"WebSocket connection closed: {e}")
        except Exception as e:
            logger.error(f"Receive error: {e}")

    async def _process_incoming_message(self, message):
        """Process incoming WebSocket message"""
        try:
            self.messages_received += 1
            
            if isinstance(message, bytes):
                # Binary message
                self.bytes_received += len(message)
                await self._handle_binary_message(message)
            else:
                # Text message
                self.bytes_received += len(message.encode('utf-8'))
                await self._handle_text_message(message)
                
        except Exception as e:
            logger.error(f"Error processing message: {e}")

    async def _handle_text_message(self, message_text):
        """Handle text message from server"""
        try:
            message_data = json.loads(message_text)
            
            # Check if it's a binary protocol message
            if message_data.get('_binary', False):
                # Decode binary data from base64
                binary_data = base64.b64decode(message_data['data'])
                await self._handle_binary_message(binary_data)
                return
            
            # Handle regular JSON message
            msg_type = message_data.get('type')
            data = message_data.get('data', {})
            
            if msg_type == 'ping':
                await self._send_pong(data.get('timestamp'))
            elif msg_type == 'pong':
                self._handle_pong(data.get('timestamp'))
            elif msg_type == 'error':
                logger.error(f"Server error: {data.get('message')}")
            elif msg_type == 'login_response':
                if data.get('success'):
                    self.session_id = data.get('session_id', 0)
                    self.player_id = data.get('player_id', 0)
            elif self.on_message:
                self.on_message(message_data)
                
            # Put in incoming queue for game state
            self.incoming_queue.put(message_data)
            
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON message: {e}")
        except Exception as e:
            logger.error(f"Error handling text message: {e}")

    async def _handle_binary_message(self, binary_data):
        """Handle binary message from server"""
        try:
            # Decompress if needed
            if self.compression_enabled and len(binary_data) > 100:
                try:
                    decompressed = zlib.decompress(binary_data)
                    binary_data = decompressed
                except Exception as e:
                    logger.error(f'{e}')

            # Parse binary message
            from .protocol import GameMessage
            message = GameMessage.deserialize(binary_data)
            
            if message:
                # Process based on message type
                handler = self._get_binary_message_handler(message.msg_type)
                if handler:
                    handler(message.data)
                else:
                    logger.warning(f"No handler for binary message type: {message.msg_type}")
                    
                # Also put in incoming queue
                self.incoming_queue.put({
                    'type': 'binary',
                    'msg_type': message.msg_type.value,
                    'data': message.data
                })
                
        except Exception as e:
            logger.error(f"Error handling binary message: {e}")

    def _get_binary_message_handler(self, msg_type):
        """Get handler for binary message type"""
        handlers = {
            MessageType.LOGIN_RESPONSE: self._handle_login_response,
            MessageType.WORLD_DATA: self._handle_world_data,
            MessageType.ENTITY_UPDATE: self._handle_entity_update,
            MessageType.CHAT_BROADCAST: self._handle_chat_message,
            MessageType.ERROR_MESSAGE: self._handle_error_message,
            MessageType.PONG: self._handle_pong_response
        }
        return handlers.get(msg_type)

    def _handle_login_response(self, data):
        """Handle binary login response"""
        parsed = MessageParser.parse_login_response(data)
        if parsed.get('success'):
            self.session_id = parsed.get('session_id', 0)
            self.player_id = parsed.get('player_id', 0)

    def _handle_world_data(self, data):
        """Handle binary world data"""
        self.incoming_queue.put({
            'type': 'world_chunk',
            'data': MessageParser.parse_world_data(data)
        })

    def _handle_entity_update(self, data):
        """Handle binary entity update"""
        self.incoming_queue.put({
            'type': 'entity_update',
            'data': MessageParser.parse_entity_update(data)
        })

    def _handle_chat_message(self, data):
        """Handle binary chat message"""
        self.incoming_queue.put({
            'type': 'chat',
            'data': MessageParser.parse_chat(data)
        })

    def _handle_error_message(self, data):
        """Handle binary error message"""
        error_data = MessageParser.parse_error(data)
        logger.error(f"Server error: {error_data.get('message')}")

    def _handle_pong_response(self, data):
        """Handle binary pong response"""
        self._handle_pong(data.get('timestamp', 0))

    def _send_loop(self):
        """Send messages to WebSocket server"""
        asyncio.run(self._send_loop_async())

    async def _send_loop_async(self):
        """Async send loop"""
        while self.running:
            try:
                if not self.ws or not self.ws.open:
                    await asyncio.sleep(0.1)
                    continue
                    
                # Get message from queue
                try:
                    message_data = self.outgoing_queue.get(timeout=0.1)
                    
                    # Check rate limiting
                    if not self._check_rate_limit():
                        logger.warning("Rate limit exceeded, dropping message")
                        continue
                    
                    # Send message
                    await self._send_message_async(message_data)
                    
                except Empty:
                    continue
                except Exception as e:
                    logger.error(f"Send error: {e}")
                    
            except Exception as e:
                logger.error(f"Send loop error: {e}")
                await asyncio.sleep(1)

    async def _send_message_async(self, message_data):
        """Send message to WebSocket server"""
        try:
            if isinstance(message_data, dict):
                # JSON message
                if self.binary_mode:
                    # Convert to binary
                    message_data['_binary'] = True
                    binary_data = self._dict_to_binary(message_data)
                    await self.ws.send(binary_data)
                else:
                    # Send as JSON
                    message_str = json.dumps(message_data)
                    await self.ws.send(message_str)
                    self.bytes_sent += len(message_str.encode('utf-8'))
                    
            elif isinstance(message_data, bytes):
                # Binary message
                if self.compression_enabled and len(message_data) > 100:
                    compressed = zlib.compress(message_data)
                    await self.ws.send(compressed)
                    self.bytes_sent += len(compressed)
                else:
                    await self.ws.send(message_data)
                    self.bytes_sent += len(message_data)
                    
            elif isinstance(message_data, str):
                # String message
                await self.ws.send(message_data)
                self.bytes_sent += len(message_data.encode('utf-8'))
                
            self.messages_sent += 1
            
        except Exception as e:
            logger.error(f"Error sending message: {e}")

    def _dict_to_binary(self, data):
        """Convert dictionary to binary format"""
        try:
            # Use msgpack if available, otherwise JSON
            import msgpack
            return msgpack.packb(data, use_bin_type=True)
        except ImportError:
            # Fallback to JSON + base64
            json_str = json.dumps(data)
            return base64.b64encode(json_str.encode('utf-8'))

    def _ping_loop(self):
        """Ping loop to keep connection alive"""
        asyncio.run(self._ping_loop_async())

    async def _ping_loop_async(self):
        """Async ping loop"""
        while self.running:
            try:
                if self.ws and self.ws.open and self.state == WebSocketState.CONNECTED:
                    # Send ping
                    await self.send_ping()
                    
                # Wait for next ping
                await asyncio.sleep(self.ping_interval)
                
            except Exception as e:
                logger.error(f"Ping loop error: {e}")
                await asyncio.sleep(1)

    async def send_ping(self):
        """Send ping to server"""
        try:
            self.last_ping_time = time.time()
            ping_data = {'timestamp': self.last_ping_time}
            
            if self.binary_mode:
                message = MessageBuilder.ping()
                await self.ws.send(message.serialize())
            else:
                await self.ws.send(json.dumps({
                    'type': 'ping',
                    'data': ping_data
                }))
                
        except Exception as e:
            logger.error(f"Error sending ping: {e}")

    async def _send_pong(self, timestamp):
        """Send pong response"""
        try:
            pong_data = {'timestamp': timestamp or time.time()}
            
            if self.binary_mode:
                # Create pong message
                from .protocol import GameMessage, MessageType
                message = GameMessage(MessageType.PONG, pong_data)
                await self.ws.send(message.serialize())
            else:
                await self.ws.send(json.dumps({
                    'type': 'pong',
                    'data': pong_data
                }))
                
        except Exception as e:
            logger.error(f"Error sending pong: {e}")

    def _handle_pong(self, timestamp):
        """Handle pong response"""
        if timestamp and self.last_ping_time:
            self.latency = (time.time() - timestamp) * 1000  # Convert to ms
            self.last_pong_time = time.time()

    def _check_rate_limit(self):
        """Check if we're within rate limits"""
        current_time = time.time()
        
        # Reset message count if we've passed the reset time
        if current_time - self.rate_limit_reset_time >= 1.0:
            self.message_count = 0
            self.rate_limit_reset_time = current_time
        
        # Check rate limit
        max_messages = self.rate_limit_config.get('messages_per_second', 100)
        if self.message_count >= max_messages:
            return False
        
        self.message_count += 1
        self.last_message_time = current_time
        return True

    def send_message(self, msg_type, data):
        """Send message to server"""
        message = {
            'type': msg_type.value if hasattr(msg_type, 'value') else msg_type,
            'data': data,
            'timestamp': time.time(),
            'session_id': self.session_id,
            'player_id': self.player_id
        }
        self.outgoing_queue.put(message)

    def send_binary_message(self, message):
        """Send binary message to server"""
        if isinstance(message, GameMessage):
            self.outgoing_queue.put(message.serialize())
        elif isinstance(message, bytes):
            self.outgoing_queue.put(message)

    def login(self, player_name=None, auth_token=None):
        """Send login request"""
        login_data = {
            'player_name': player_name or 'Player',
            'auth_token': auth_token or '',
            'version': '1.0.0'
        }
        self.send_message('login', login_data)

    def send_movement(self, position, rotation, velocity):
        """Send player movement update"""
        movement_data = {
            'position': position,
            'rotation': rotation,
            'velocity': velocity,
            'timestamp': time.time()
        }
        self.send_message('movement', movement_data)

    def send_chat(self, message, channel='global'):
        """Send chat message"""
        chat_data = {
            'message': message,
            'channel': channel
        }
        self.send_message('chat', chat_data)

    def disconnect(self):
        """Disconnect from WebSocket server"""
        self.running = False
        self.state = WebSocketState.CLOSING
        
        # Wait for threads to finish
        if self.receive_thread:
            self.receive_thread.join(timeout=2.0)
        if self.send_thread:
            self.send_thread.join(timeout=2.0)
        if self.ping_thread:
            self.ping_thread.join(timeout=2.0)
        
        self.state = WebSocketState.CLOSED
        logger.info("WebSocket disconnected")

    def is_connected(self):
        """Check if connected to server"""
        return (self.state == WebSocketState.CONNECTED and 
                self.ws and self.ws.open)

    def get_stats(self):
        """Get connection statistics"""
        return {
            'state': self.state.value,
            'messages_sent': self.messages_sent,
            'messages_received': self.messages_received,
            'bytes_sent': self.bytes_sent,
            'bytes_received': self.bytes_received,
            'latency_ms': self.latency,
            'reconnect_count': self.reconnect_count,
            'session_id': self.session_id,
            'player_id': self.player_id,
            'queue_size': self.outgoing_queue.qsize()
        }
