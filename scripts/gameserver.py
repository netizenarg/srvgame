#!/usr/bin/env python3
"""
Python module exposing C++ game server functions to scripts.
This module is automatically imported by the C++ Python scripting engine.
"""

import json
import random
import time

# These functions are implemented in C++ and exposed via Python/C API
# Stub implementations are provided for development/testing

class GameServer:
    """Wrapper class for C++ game server functions"""
    
    @staticmethod
    def log_debug(message):
        """Log debug message"""
        print(f"[DEBUG] {message}")
    
    @staticmethod
    def log_info(message):
        """Log info message"""
        print(f"[INFO] {message}")
    
    @staticmethod
    def log_warning(message):
        """Log warning message"""
        print(f"[WARN] {message}")
    
    @staticmethod
    def log_error(message):
        """Log error message"""
        print(f"[ERROR] {message}")
    
    @staticmethod
    def log_critical(message):
        """Log critical message"""
        print(f"[CRITICAL] {message}")
    
    @staticmethod
    def get_player(player_id):
        """Get player data by ID"""
        # This will be implemented in C++
        return {"id": player_id, "name": f"Player_{player_id}", "level": 1}
    
    @staticmethod
    def set_player_position(player_id, x, y, z):
        """Set player position"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def give_player_item(player_id, item_id, count):
        """Give item to player"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def take_player_item(player_id, item_id, count):
        """Take item from player"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def add_player_experience(player_id, amount):
        """Add experience to player"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def set_player_health(player_id, health):
        """Set player health"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def set_player_mana(player_id, mana):
        """Set player mana"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def teleport_player(player_id, x, y, z):
        """Teleport player to position"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def send_message_to_player(player_id, message):
        """Send message to player"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def broadcast_to_nearby(player_id, message, radius):
        """Broadcast message to nearby players"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def query_database(query, params=None):
        """Execute database query with parameters"""
        # This will be implemented in C++
        if params:
            # Sanitize parameters
            sanitized_params = []
            for param in params:
                if isinstance(param, str):
                    # Escape single quotes for SQL
                    sanitized = param.replace("'", "''")
                    sanitized_params.append(sanitized)
                else:
                    sanitized_params.append(param)
            
            # For development, just return empty result
            return {"success": True, "data": []}
        
        return {"success": True, "data": []}
    
    @staticmethod
    def execute_database(query, params=None):
        """Execute database command with parameters"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def get_player_from_db(player_id):
        """Get player data from database"""
        # This will be implemented in C++
        return {"id": player_id, "name": f"Player_{player_id}", "level": 1}
    
    @staticmethod
    def save_player_to_db(player_id, data):
        """Save player data to database"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def fire_event(event_name, data=None):
        """Fire game event"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def schedule_event(delay_ms, event_name, data=None):
        """Schedule delayed event"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def get_current_time():
        """Get current timestamp in milliseconds"""
        return int(time.time() * 1000)
    
    @staticmethod
    def generate_uuid():
        """Generate UUID"""
        import uuid
        return str(uuid.uuid4())
    
    @staticmethod
    def parse_json(json_str):
        """Parse JSON string"""
        return json.loads(json_str)
    
    @staticmethod
    def stringify_json(data):
        """Convert data to JSON string"""
        return json.dumps(data)
    
    @staticmethod
    def random_float(min_val, max_val):
        """Generate random float"""
        return random.uniform(min_val, max_val)
    
    @staticmethod
    def random_int(min_val, max_val):
        """Generate random integer"""
        return random.randint(min_val, max_val)
    
    @staticmethod
    def distance(x1, y1, z1, x2, y2, z2):
        """Calculate distance between two points"""
        import math
        return math.sqrt((x2 - x1) ** 2 + (y2 - y1) ** 2 + (z2 - z1) ** 2)
    
    @staticmethod
    def get_config(key):
        """Get configuration value"""
        # This will be implemented in C++
        return None
    
    @staticmethod
    def set_config(key, value):
        """Set configuration value"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def register_event_handler(event_name, handler_func):
        """Register Python event handler"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def get_player_stats(player_id):
        """Get player statistics"""
        # This will be implemented in C++
        return {"level": 1, "luck": 0}
    
    @staticmethod
    def get_player_level(player_id):
        """Get player level"""
        # This will be implemented in C++
        return 1
    
    @staticmethod
    def add_player_gold(player_id, amount):
        """Add gold to player"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def create_loot_entity(x, y, z, item_id, quantity):
        """Create loot entity in world"""
        # This will be implemented in C++
        return 0
    
    @staticmethod
    def has_player_completed_quest(player_id, quest_id):
        """Check if player has completed quest"""
        # This will be implemented in C++
        return False

# Create singleton instance
server = GameServer()

# Helper function to validate event data
def validate_event_data(event_data, required_fields):
    """Validate event data contains required fields"""
    if 'data' not in event_data:
        return False, "Event data missing 'data' field"
    
    data = event_data['data']
    for field in required_fields:
        if field not in data:
            return False, f"Missing required field: {field}"
    
    return True, ""

# Helper function for safe database operations
class SafeDatabase:
    """Safe database operation wrapper"""
    
    @staticmethod
    def execute_query(server_instance, query, params=None):
        """Execute query with parameter validation"""
        if params is None:
            params = ()
        
        # Validate parameters are not malicious
        for param in params:
            if isinstance(param, str) and any(char in param for char in [";", "--", "/*", "*/"]):
                server_instance.log_error(f"Potential SQL injection detected in parameter: {param}")
                return {"success": False, "error": "Invalid parameter"}
        
        return server_instance.query_database(query, params)
    
    @staticmethod
    def execute_command(server_instance, query, params=None):
        """Execute command with parameter validation"""
        if params is None:
            params = ()
        
        # Validate parameters
        for param in params:
            if isinstance(param, str) and any(char in param for char in [";", "--", "/*", "*/"]):
                server_instance.log_error(f"Potential SQL injection detected in parameter: {param}")
                return False
        
        return server_instance.execute_database(query, params)

# Expose the server instance
__all__ = ['server', 'validate_event_data', 'SafeDatabase']
