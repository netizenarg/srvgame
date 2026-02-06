#!/usr/bin/env python3
"""
Ogre3D + Kivy Game Client for C++ Game Server
"""

import sys
import logging
import yaml
import json
import threading
from pathlib import Path

import kivy
kivy.require('2.3.0')

from kivy.app import App
from kivy.clock import Clock
from kivy.core.window import Window
from kivy.uix.boxlayout import BoxLayout

# Import from network module
from network import create_network_client
from renderer import Ogre3DRenderer
from gui import GameUI
from game import GameStateManager

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class OgreKivyGameClient(BoxLayout):
    """Main container for Ogre3D view and Kivy UI"""

    def __init__(self, config, **kwargs):
        super().__init__(**kwargs)
        self.config = config
        self.orientation = 'vertical'

        # Initialize components
        self.game_state = GameStateManager()
        
        # Load network configuration
        network_config_path = config.get('network_config_path', 'config/network_config.json')
        
        # Create network client based on configuration
        self.network_client = create_network_client(network_config_path, self.game_state)
        
        # Set up WebSocket event handlers if using WebSocket
        if hasattr(self.network_client, 'set_event_handlers'):
            self.network_client.set_event_handlers(
                on_connect=self.on_network_connect,
                on_disconnect=self.on_network_disconnect,
                on_error=self.on_network_error,
                on_message=self.on_network_message
            )

        # Create Ogre3D renderer (will run in separate thread)
        self.ogre_renderer = Ogre3DRenderer(self.game_state, config)

        # Create Kivy UI
        self.ui = GameUI(self.game_state, self.network_client, self.ogre_renderer)

        # Add UI to layout
        self.add_widget(self.ui)

        # Start networking in background thread
        self.network_thread = threading.Thread(
            target=self.network_client.connect,
            daemon=True
        )
        self.network_thread.start()

        # Start Ogre3D renderer in separate thread
        self.ogre_thread = threading.Thread(
            target=self.ogre_renderer.run,
            daemon=True
        )
        self.ogre_thread.start()

        # Schedule updates
        Clock.schedule_interval(self.update, 1.0 / 60.0)

    def on_network_connect(self):
        """Handler for network connection established"""
        logger.info("Network connected")
        
        # Send login after connection
        player_name = self.config.get('client', {}).get('player_name', 'Player1')
        self.network_client.login(player_name=player_name)

    def on_network_disconnect(self):
        """Handler for network disconnection"""
        logger.warning("Network disconnected")

    def on_network_error(self, error_msg):
        """Handler for network errors"""
        logger.error(f"Network error: {error_msg}")

    def on_network_message(self, message):
        """Handler for network messages (when not using standard handlers)"""
        # This is called for custom message types
        logger.debug(f"Network message: {message.get('type')}")

    def update(self, dt):
        """Main game loop update"""
        if hasattr(self.network_client, 'is_connected'):
            connected = self.network_client.is_connected()
        else:
            connected = self.network_client.connected

        if connected:
            # Process received messages
            if hasattr(self.network_client, 'process_messages'):
                self.network_client.process_messages()
            elif hasattr(self.game_state, 'apply_server_update'):
                # Process messages from queue
                while not self.network_client.incoming_queue.empty():
                    try:
                        message = self.network_client.incoming_queue.get_nowait()
                        self.game_state.apply_server_update(message)
                    except Exception as e:
                        logger.error(f"Error processing message: {e}")

            # Update game state
            self.game_state.update(dt)

            # Sync with Ogre3D renderer
            self.ogre_renderer.sync_state(self.game_state)

            # Update UI
            self.ui.update(dt)
            
            # Update network stats in UI if available
            if hasattr(self.network_client, 'get_stats'):
                stats = self.network_client.get_stats()
                self.ui.update_network_stats(stats)


class GameClientApp(App):
    """Main Kivy Application"""

    def __init__(self, config, **kwargs):
        super().__init__(**kwargs)
        self.config = config
        self.title = f"Game Client - {config['client']['player_name']}"

    def build(self):
        Window.size = (config['window']['width'], config['window']['height'])
        Window.minimum_width = 800
        Window.minimum_height = 600

        # Set window icon
        icon_path = Path("assets/ui/window_icon.png")
        if icon_path.exists():
            Window.set_icon(str(icon_path))

        return OgreKivyGameClient(self.config)

    def on_stop(self):
        """Cleanup on application exit"""
        logger.info("Shutting down game client...")
        if hasattr(self.root, 'network_client'):
            self.root.network_client.disconnect()
        if hasattr(self.root, 'ogre_renderer'):
            self.root.ogre_renderer.shutdown()
        return True


def load_config(config_path="config/client_config.yaml"):
    """Load client configuration"""
    try:
        if os.path.exists(config_path):
            with open(config_path, 'r') as f:
                config = yaml.safe_load(f)
        else:
            # Default configuration
            default_config = {
                'server': {
                    'host': '127.0.0.1',
                    'port': 8080,
                    'protocol': 'tcp',
                    'reconnect_interval': 5
                },
                'client': {
                    'player_name': 'Player1',
                    'render_distance': 1000.0,
                    'fov': 70.0,
                    'vsync': True,
                    'fullscreen': False,
                    'network_config_path': 'config/network_config.json'
                },
                'window': {
                    'width': 1280,
                    'height': 720,
                    'title': 'Ogre3D Game Client'
                },
                'graphics': {
                    'shadow_quality': 'medium',
                    'texture_quality': 'high',
                    'antialiasing': 4,
                    'anisotropic_filtering': 8
                },
                'controls': {
                    'mouse_sensitivity': 0.5,
                    'invert_mouse_y': False,
                    'movement_speed': 10.0
                },
                'ogre': {
                    'plugins_path': 'plugins.cfg',
                    'resources_path': 'resources.cfg',
                    'log_path': 'ogre.log'
                }
            }

            # Create directory and save default config
            Path(config_path).parent.mkdir(exist_ok=True)
            with open(config_path, 'w') as f:
                yaml.dump(default_config, f, default_flow_style=False)

            config = default_config
        
        return config
        
    except Exception as e:
        logger.error(f"Error loading config: {e}")
        raise


if __name__ == '__main__':
    config = load_config()

    # Handle command line arguments
    import argparse
    parser = argparse.ArgumentParser(description='Ogre3D Game Client')
    parser.add_argument('--host', help='Server host address')
    parser.add_argument('--port', type=int, help='Server port')
    parser.add_argument('--player', help='Player name')
    parser.add_argument('--fullscreen', action='store_true', help='Start in fullscreen')
    parser.add_argument('--protocol', choices=['websocket', 'tcp', 'udp'], help='Network protocol')
    parser.add_argument('--config', help='Network config file path')

    args = parser.parse_args()

    # Override config with command line arguments
    if args.host:
        config['server']['host'] = args.host
    if args.port:
        config['server']['port'] = args.port
    if args.player:
        config['client']['player_name'] = args.player
    if args.fullscreen:
        config['client']['fullscreen'] = True
    if args.protocol:
        config['server']['protocol'] = args.protocol
    if args.config:
        config['client']['network_config_path'] = args.config

    # Run the application
    app = GameClientApp(config)
    app.run()