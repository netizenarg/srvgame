#!/usr/bin/env python3
"""
Python module exposing C++ game server functions to scripts.
This module is automatically imported by the C++ Python scripting engine.

SECURITY NOTE: This module handles player data and game state - all functions
must be implemented with security in mind in the C++ layer.
"""

import json
import time
import hashlib
import hmac
import secrets
from typing import Any, Dict, List, Optional, Tuple, Union

# These functions are implemented in C++ and exposed via Python/C API
# Stub implementations are provided for development/testing

class GameServer:
    """Wrapper class for C++ game server functions"""
    
    # Security configuration
    _SECURE_RANDOM_MIN_BYTES = 16  # Minimum bytes for secure random
    
    @staticmethod
    def log_debug(message: str) -> None:
        """Log debug message"""
        print(f"[DEBUG] {message}")
    
    @staticmethod
    def log_info(message: str) -> None:
        """Log info message"""
        print(f"[INFO] {message}")
    
    @staticmethod
    def log_warning(message: str) -> None:
        """Log warning message"""
        print(f"[WARN] {message}")
    
    @staticmethod
    def log_error(message: str) -> None:
        """Log error message"""
        print(f"[ERROR] {message}")
    
    @staticmethod
    def log_critical(message: str) -> None:
        """Log critical message"""
        print(f"[CRITICAL] {message}")
    
    @staticmethod
    def get_player(player_id: int) -> Dict[str, Any]:
        """Get player data by ID"""
        # This will be implemented in C++
        # SECURITY: Validate player_id is positive integer
        if not isinstance(player_id, int) or player_id <= 0:
            GameServer.log_error(f"Invalid player_id: {player_id}")
            return {}
        return {"id": player_id, "name": f"Player_{player_id}", "level": 1}
    
    @staticmethod
    def set_player_position(player_id: int, x: float, y: float, z: float) -> bool:
        """Set player position with validation"""
        # This will be implemented in C++
        # SECURITY: Validate all inputs
        if not all(isinstance(v, (int, float)) for v in [x, y, z]):
            GameServer.log_error(f"Invalid position values: {x}, {y}, {z}")
            return False
        return True
    
    @staticmethod
    def give_player_item(player_id: int, item_id: int, count: int) -> bool:
        """Give item to player with validation"""
        # This will be implemented in C++
        if not isinstance(count, int) or count <= 0:
            GameServer.log_error(f"Invalid item count: {count}")
            return False
        if count > 1000:  # Reasonable limit
            GameServer.log_warning(f"Large item transfer: {count} items")
        return True
    
    @staticmethod
    def take_player_item(player_id: int, item_id: int, count: int) -> bool:
        """Take item from player with validation"""
        # This will be implemented in C++
        if not isinstance(count, int) or count <= 0:
            GameServer.log_error(f"Invalid item count: {count}")
            return False
        return True
    
    @staticmethod
    def add_player_experience(player_id: int, amount: int) -> bool:
        """Add experience to player with validation"""
        # This will be implemented in C++
        if not isinstance(amount, int) or amount < 0:
            GameServer.log_error(f"Invalid experience amount: {amount}")
            return False
        return True
    
    @staticmethod
    def set_player_health(player_id: int, health: float) -> bool:
        """Set player health with validation"""
        # This will be implemented in C++
        if not isinstance(health, (int, float)) or health < 0:
            GameServer.log_error(f"Invalid health value: {health}")
            return False
        return True
    
    @staticmethod
    def set_player_mana(player_id: int, mana: float) -> bool:
        """Set player mana with validation"""
        # This will be implemented in C++
        if not isinstance(mana, (int, float)) or mana < 0:
            GameServer.log_error(f"Invalid mana value: {mana}")
            return False
        return True
    
    @staticmethod
    def teleport_player(player_id: int, x: float, y: float, z: float) -> bool:
        """Teleport player to position with validation"""
        # This will be implemented in C++
        # SECURITY: Add teleport cooldown/rate limiting in C++ implementation
        if not all(isinstance(v, (int, float)) for v in [x, y, z]):
            GameServer.log_error(f"Invalid teleport position: {x}, {y}, {z}")
            return False
        return True
    
    @staticmethod
    def send_message_to_player(player_id: int, message: str) -> bool:
        """Send message to player with sanitization"""
        # This will be implemented in C++
        if not isinstance(message, str):
            GameServer.log_error("Message must be a string")
            return False
        
        # Sanitize message length
        if len(message) > 1000:
            GameServer.log_warning(f"Long message truncated: {len(message)} chars")
            message = message[:1000]
        
        return True
    
    @staticmethod
    def broadcast_to_nearby(player_id: int, message: str, radius: float) -> bool:
        """Broadcast message to nearby players with validation"""
        # This will be implemented in C++
        if not isinstance(radius, (int, float)) or radius < 0:
            GameServer.log_error(f"Invalid broadcast radius: {radius}")
            return False
        if radius > 1000:  # Reasonable limit
            GameServer.log_warning(f"Large broadcast radius: {radius}")
        return True
    
    @staticmethod
    def query_database(query: str, params: Optional[Union[tuple, list]] = None) -> Dict[str, Any]:
        """Execute database query with parameterized statements"""
        # This will be implemented in C++
        # SECURITY: C++ implementation MUST use prepared statements/parameterized queries
        if params:
            # Additional validation in Python layer
            for param in params:
                if isinstance(param, str) and len(param) > 10000:
                    GameServer.log_error("Parameter too large")
                    return {"success": False, "error": "Parameter too large"}
        
        return {"success": True, "data": []}
    
    @staticmethod
    def execute_database(query: str, params: Optional[Union[tuple, list]] = None) -> bool:
        """Execute database command with parameterized statements"""
        # This will be implemented in C++
        # SECURITY: C++ implementation MUST use prepared statements
        return True
    
    @staticmethod
    def get_player_from_db(player_id: int) -> Dict[str, Any]:
        """Get player data from database"""
        # This will be implemented in C++
        return {"id": player_id, "name": f"Player_{player_id}", "level": 1}
    
    @staticmethod
    def save_player_to_db(player_id: int, data: Dict[str, Any]) -> bool:
        """Save player data to database"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def fire_event(event_name: str, data: Optional[Dict[str, Any]] = None) -> bool:
        """Fire game event with validation"""
        # This will be implemented in C++
        if not isinstance(event_name, str):
            return False
        if len(event_name) > 100:
            GameServer.log_warning(f"Long event name: {event_name}")
        return True
    
    @staticmethod
    def schedule_event(delay_ms: int, event_name: str, data: Optional[Dict[str, Any]] = None) -> bool:
        """Schedule delayed event with validation"""
        # This will be implemented in C++
        if not isinstance(delay_ms, int) or delay_ms < 0:
            GameServer.log_error(f"Invalid delay: {delay_ms}")
            return False
        if delay_ms > 3600000:  # 1 hour maximum
            GameServer.log_warning(f"Long scheduled event delay: {delay_ms}ms")
        return True
    
    @staticmethod
    def get_current_time() -> int:
        """Get current timestamp in milliseconds"""
        return int(time.time() * 1000)
    
    @staticmethod
    def generate_uuid() -> str:
        """Generate UUID using cryptographically secure method"""
        import uuid
        return str(uuid.uuid4())  # uuid4 uses os.urandom (cryptographically secure)
    
    @staticmethod
    def parse_json(json_str: str) -> Any:
        """Parse JSON string with size limits"""
        if len(json_str) > 1000000:  # 1MB limit
            raise ValueError("JSON string too large")
        return json.loads(json_str)
    
    @staticmethod
    def stringify_json(data: Any) -> str:
        """Convert data to JSON string"""
        return json.dumps(data)
    
    @staticmethod
    def random_float(min_val: float, max_val: float) -> float:
        """Generate random float - NOT for cryptographic use"""
        import random
        if min_val >= max_val:
            raise ValueError("min_val must be less than max_val")
        return random.uniform(min_val, max_val)
    
    @staticmethod
    def random_int(min_val: int, max_val: int) -> int:
        """Generate random integer - NOT for cryptographic use"""
        import random
        if min_val >= max_val:
            raise ValueError("min_val must be less than max_val")
        return random.randint(min_val, max_val)
    
    @staticmethod
    def secure_random_int(min_val: int, max_val: int) -> int:
        """Generate cryptographically secure random integer"""
        if min_val >= max_val:
            raise ValueError("min_val must be less than max_val")
        
        # Calculate range and required bits
        range_size = max_val - min_val + 1
        bits_needed = range_size.bit_length()
        bytes_needed = (bits_needed + 7) // 8
        
        # Ensure minimum security
        bytes_to_get = max(bytes_needed, GameServer._SECURE_RANDOM_MIN_BYTES)
        
        # Get secure random bytes
        random_bytes = secrets.token_bytes(bytes_to_get)
        
        # Convert to integer in range
        random_value = int.from_bytes(random_bytes, 'big')
        result = min_val + (random_value % range_size)
        
        return result
    
    @staticmethod
    def secure_random_float(min_val: float, max_val: float) -> float:
        """Generate cryptographically secure random float"""
        if min_val >= max_val:
            raise ValueError("min_val must be less than max_val")
        
        # Get secure random bytes
        random_bytes = secrets.token_bytes(8)
        random_value = int.from_bytes(random_bytes, 'big')
        
        # Convert to float in range [0, 1)
        max_int = 1 << 53  # 53 bits for double precision mantissa
        scaled = (random_value % max_int) / max_int
        
        return min_val + scaled * (max_val - min_val)
    
    @staticmethod
    def generate_secure_token(length: int = 32) -> str:
        """Generate cryptographically secure random token"""
        if length < 16:
            length = 16  # Minimum length for security
        if length > 1024:
            length = 1024  # Maximum reasonable length
        
        return secrets.token_urlsafe(length)
    
    @staticmethod
    def calculate_hmac(data: str, secret_key: str) -> str:
        """Calculate HMAC for data verification"""
        return hmac.new(
            secret_key.encode('utf-8'),
            data.encode('utf-8'),
            hashlib.sha256
        ).hexdigest()
    
    @staticmethod
    def distance(x1: float, y1: float, z1: float, x2: float, y2: float, z2: float) -> float:
        """Calculate distance between two points"""
        import math
        
        # Validate inputs
        if not all(isinstance(v, (int, float)) for v in [x1, y1, z1, x2, y2, z2]):
            raise ValueError("All coordinates must be numbers")
        
        return math.sqrt((x2 - x1) ** 2 + (y2 - y1) ** 2 + (z2 - z1) ** 2)
    
    @staticmethod
    def get_config(key: str) -> Any:
        """Get configuration value"""
        # This will be implemented in C++
        return None
    
    @staticmethod
    def set_config(key: str, value: Any) -> bool:
        """Set configuration value"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def register_event_handler(event_name: str, handler_func: callable) -> bool:
        """Register Python event handler"""
        # This will be implemented in C++
        return True
    
    @staticmethod
    def get_player_stats(player_id: int) -> Dict[str, Any]:
        """Get player statistics"""
        # This will be implemented in C++
        return {"level": 1, "luck": 0}
    
    @staticmethod
    def get_player_level(player_id: int) -> int:
        """Get player level"""
        # This will be implemented in C++
        return 1
    
    @staticmethod
    def add_player_gold(player_id: int, amount: int) -> bool:
        """Add gold to player with validation"""
        # This will be implemented in C++
        if not isinstance(amount, int):
            GameServer.log_error(f"Invalid gold amount type: {type(amount)}")
            return False
        if amount > 1000000:  # Reasonable limit
            GameServer.log_warning(f"Large gold transfer: {amount}")
        return True
    
    @staticmethod
    def create_loot_entity(x: float, y: float, z: float, item_id: int, quantity: int) -> int:
        """Create loot entity in world"""
        # This will be implemented in C++
        return 0
    
    @staticmethod
    def has_player_completed_quest(player_id: int, quest_id: int) -> bool:
        """Check if player has completed quest"""
        # This will be implemented in C++
        return False
    
    @staticmethod
    def sanitize_string(input_str: str, max_length: int = 1000) -> str:
        """Sanitize string input by removing dangerous characters and limiting length"""
        if not isinstance(input_str, str):
            return ""
        
        # Remove control characters and excessive whitespace
        import re
        sanitized = re.sub(r'[\x00-\x1F\x7F]', '', input_str)
        sanitized = re.sub(r'\s+', ' ', sanitized).strip()
        
        # Limit length
        if len(sanitized) > max_length:
            sanitized = sanitized[:max_length]
            GameServer.log_warning(f"String truncated to {max_length} characters")
        
        return sanitized

# Create singleton instance
server = GameServer()

# Helper function to validate event data
def validate_event_data(event_data: Dict[str, Any], required_fields: List[str]) -> Tuple[bool, str]:
    """Validate event data contains required fields"""
    if not isinstance(event_data, dict):
        return False, "Event data must be a dictionary"
    
    if 'data' not in event_data:
        return False, "Event data missing 'data' field"
    
    data = event_data['data']
    if not isinstance(data, dict):
        return False, "Event data field must be a dictionary"
    
    for field in required_fields:
        if field not in data:
            return False, f"Missing required field: {field}"
    
    return True, ""

# Helper function for safe database operations
class SafeDatabase:
    """Safe database operation wrapper"""
    
    @staticmethod
    def execute_query(server_instance: GameServer, query: str, params: Optional[Union[tuple, list]] = None) -> Dict[str, Any]:
        """Execute query with parameter validation"""
        if params is None:
            params = ()
        
        # Type validation
        if not isinstance(query, str):
            server_instance.log_error("Query must be a string")
            return {"success": False, "error": "Invalid query type"}
        
        if not isinstance(params, (tuple, list)):
            server_instance.log_error("Parameters must be tuple or list")
            return {"success": False, "error": "Invalid parameters type"}
        
        # Query length validation
        if len(query) > 10000:
            server_instance.log_error("Query too long")
            return {"success": False, "error": "Query too long"}
        
        # Basic SQL injection detection (additional layer - main protection should be in C++)
        sql_keywords = ["DROP", "DELETE", "UPDATE", "INSERT", "ALTER", "CREATE", "TRUNCATE"]
        query_upper = query.upper()
        
        for keyword in sql_keywords:
            if f" {keyword} " in query_upper and "?" not in query and "%s" not in query:
                server_instance.log_warning(f"Potential unsafe query detected with keyword: {keyword}")
        
        return server_instance.query_database(query, params)
    
    @staticmethod
    def execute_command(server_instance: GameServer, query: str, params: Optional[Union[tuple, list]] = None) -> bool:
        """Execute command with parameter validation"""
        if params is None:
            params = ()
        
        # Same validation as execute_query
        if not isinstance(query, str):
            server_instance.log_error("Query must be a string")
            return False
        
        if not isinstance(params, (tuple, list)):
            server_instance.log_error("Parameters must be tuple or list")
            return False
        
        if len(query) > 10000:
            server_instance.log_error("Query too long")
            return False
        
        return server_instance.execute_database(query, params)
    
    @staticmethod
    def sanitize_identifier(identifier: str) -> str:
        """Sanitize SQL identifier (table/column name)"""
        # Very basic sanitization - actual SQL should use parameterized queries
        import re
        # Allow alphanumeric and underscores
        return re.sub(r'[^a-zA-Z0-9_]', '', identifier)

# Security utilities
class SecurityUtils:
    """Security-related utility functions"""
    
    @staticmethod
    def validate_integer(value: Any, min_val: Optional[int] = None, max_val: Optional[int] = None) -> bool:
        """Validate integer value with optional range"""
        if not isinstance(value, int):
            return False
        if min_val is not None and value < min_val:
            return False
        if max_val is not None and value > max_val:
            return False
        return True
    
    @staticmethod
    def validate_float(value: Any, min_val: Optional[float] = None, max_val: Optional[float] = None) -> bool:
        """Validate float value with optional range"""
        if not isinstance(value, (int, float)):
            return False
        if min_val is not None and value < min_val:
            return False
        if max_val is not None and value > max_val:
            return False
        return True
    
    @staticmethod
    def generate_password_hash(password: str, salt: Optional[str] = None) -> Dict[str, str]:
        """Generate secure password hash using PBKDF2"""
        import os
        import base64
        
        if salt is None:
            salt = secrets.token_bytes(16)
        elif isinstance(salt, str):
            salt = base64.b64decode(salt)
        
        # Use PBKDF2 with SHA256
        hash_result = hashlib.pbkdf2_hmac(
            'sha256',
            password.encode('utf-8'),
            salt,
            100000,  # 100k iterations
            dklen=32
        )
        
        return {
            'hash': base64.b64encode(hash_result).decode('utf-8'),
            'salt': base64.b64encode(salt).decode('utf-8'),
            'algorithm': 'pbkdf2_sha256'
        }
    
    @staticmethod
    def verify_password(password: str, stored_hash: str, salt: str) -> bool:
        """Verify password against stored hash"""
        import base64
        
        try:
            salt_bytes = base64.b64decode(salt)
            hash_bytes = base64.b64decode(stored_hash)
            
            # Generate hash with same parameters
            test_hash = hashlib.pbkdf2_hmac(
                'sha256',
                password.encode('utf-8'),
                salt_bytes,
                100000,
                dklen=32
            )
            
            # Constant-time comparison to prevent timing attacks
            return hmac.compare_digest(hash_bytes, test_hash)
        except:
            return False

# Expose the server instance and utilities
__all__ = [
    'server', 
    'validate_event_data', 
    'SafeDatabase',
    'SecurityUtils'
]

# Security warning about random functions
__SECURITY_WARNING__ = """
WARNING: The random_int() and random_float() functions use Python's standard 
random module which is NOT cryptographically secure. For security-sensitive 
operations (tokens, passwords, cryptographic keys, etc.), use:
- secure_random_int()
- secure_random_float() 
- generate_secure_token()
- generate_uuid() (for UUIDs)
"""

# Print security warning on import
print(__SECURITY_WARNING__)