"""
Network client for connecting to C++ game server with multiple protocol support
"""

import json
import logging
import os
from pathlib import Path
from typing import Dict, Any

from .protocol import MessageType, GameMessage, MessageBuilder, MessageParser

# Import appropriate client based on configuration
def create_network_client(config_path="config/network.json", game_state=None):
    """
    Create network client based on configuration
    
    Args:
        config_path: Path to network configuration file
        game_state: GameStateManager instance
    
    Returns:
        Network client instance (WebSocket or TCP/UDP)
    """
    # Load configuration
    config = load_network_config(config_path)
    
    protocol = config.get('network', {}).get('protocol', 'websocket').lower()
    
    if protocol == 'websocket':
        from .websocket_client import WebSocketGameClient
        ws_config = config.get('network', {}).get('websocket', {})
        
        client = WebSocketGameClient(ws_config, game_state)
        
        # Set binary mode if configured
        binary_config = config.get('network', {}).get('binary_protocol', {})
        if binary_config.get('enabled', False):
            client.binary_mode = True
            
        return client
        
    elif protocol in ['tcp', 'udp']:
        from .tcp_udp_client import TcpUdpClient
        
        tcp_config = config.get('network', {}).get('tcp_udp', {})
        use_udp = (protocol == 'udp')
        
        client = TcpUdpClient(
            tcp_config.get('host', '127.0.0.1'),
            tcp_config.get('port', 8080),
            game_state,
            use_udp=use_udp
        )
        
        return client
        
    else:
        raise ValueError(f"Unsupported protocol: {protocol}")

def load_network_config(config_path):
    """Load network configuration from JSON file"""
    try:
        if os.path.exists(config_path):
            with open(config_path, 'r') as f:
                return json.load(f)
        else:
            # Create default configuration
            return create_default_config(config_path)
    except Exception as e:
        logging.error(f"Error loading network config: {e}")
        return create_default_config(config_path)

def create_default_config(config_path):
    """Create default network configuration"""
    default_config = {
        "network": {
            "protocol": "websocket",
            "binary_protocol": {
                "enabled": False,
                "compression_level": 6,
                "encryption_enabled": False,
                "max_message_size": 10485760,
                "reliable_messages": True
            },
            "websocket": {
                "enabled": True,
                "url": "ws://127.0.0.1:8080/ws",
                "auto_reconnect": True,
                "reconnect_delay": 5,
                "max_reconnect_attempts": 10,
                "ping_interval": 30,
                "ping_timeout": 10,
                "use_ssl": False,
                "verify_ssl": True,
                "protocols": ["game-protocol-v1"],
                "compression_enabled": True,
                "max_frame_size": 16384,
                "rate_limit": {
                    "messages_per_second": 100,
                    "burst_size": 1000
                }
            },
            "tcp_udp": {
                "enabled": False,
                "host": "127.0.0.1",
                "port": 8080,
                "use_udp": False,
                "timeout": 5,
                "buffer_size": 4096,
                "keepalive": True,
                "no_delay": True
            },
            "performance": {
                "max_outgoing_queue": 1000,
                "max_incoming_queue": 1000,
                "worker_threads": 2,
                "process_messages_per_frame": 10
            },
            "debug": {
                "log_network_traffic": False,
                "log_messages": False,
                "log_errors": True,
                "log_latency": True
            }
        }
    }
    
    # Save default config
    config_dir = Path(config_path).parent
    config_dir.mkdir(exist_ok=True)
    
    with open(config_path, 'w') as f:
        json.dump(default_config, f, indent=2)
    
    logging.info(f"Created default network config at {config_path}")
    return default_config

# Export main classes
__all__ = [
    'MessageType',
    'GameMessage',
    'MessageBuilder',
    'MessageParser',
    'create_network_client',
    'load_network_config'
]
