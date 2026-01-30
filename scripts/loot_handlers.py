import random
import secrets
import time
from typing import Dict, List, Any, Optional
import hashlib
import hmac

class LootHandler:
    def __init__(self, server):
        self.server = server
        self.loot_tables = {}
        self.item_templates = {}
        # Use SystemRandom for cryptographic-strength randomness
        self.secure_rng = random.SystemRandom()
        # Server-wide seed for deterministic but unpredictable loot
        self.server_seed = self.generate_server_seed()
        
    def generate_server_seed(self) -> str:
        """Generate a cryptographically secure seed for the server session"""
        # Combine current time, process ID, and random bytes
        seed_data = f"{time.time()}{secrets.token_hex(16)}".encode()
        return hashlib.sha256(seed_data).hexdigest()
    
    def generate_deterministic_random(self, player_id: str, mob_id: str, 
                                     sequence_num: int) -> float:
        """
        Generate deterministic but unpredictable random numbers for loot.
        This prevents prediction exploits while maintaining fairness.
        """
        # Create a unique input for HMAC
        unique_input = f"{self.server_seed}:{player_id}:{mob_id}:{sequence_num}"
        
        # Use HMAC with server seed as key for unpredictability
        hmac_obj = hmac.new(
            self.server_seed.encode(),
            unique_input.encode(),
            hashlib.sha256
        )
        
        # Convert first 8 bytes to a float between 0 and 1
        random_bytes = hmac_obj.digest()[:8]
        random_int = int.from_bytes(random_bytes, byteorder='big')
        return random_int / (2**64)  # Scale to 0-1 range
    
    def register_event_handlers(self):
        """Register Python event handlers for loot system"""
        self.server.register_event_handler('mob_death', self.on_mob_death)
        self.server.register_event_handler('chest_opened', self.on_chest_opened)
        self.server.register_event_handler('quest_completed', self.on_quest_completed)
        self.server.register_event_handler('player_level_up', self.on_player_level_up)
        
    def on_mob_death(self, event_data: Dict[str, Any]) -> bool:
        """Handle mob death and generate loot"""
        mob_id = event_data['data']['mobId']
        killer_id = event_data['data']['killerId']
        mob_type = event_data['data']['mobType']
        mob_level = event_data['data'].get('level', 1)
        
        # Validate player can receive loot
        if not self.validate_player_can_receive_loot(killer_id):
            self.server.log_warning(f"Player {killer_id} cannot receive loot (possible cheat)")
            return False
        
        # Get player's luck stat
        player_stats = self.server.get_player_stats(killer_id)
        luck_multiplier = 1.0 + (player_stats.get('luck', 0) / 100.0)
        
        # Determine loot table based on mob type
        loot_table_name = self.get_mob_loot_table(mob_type, mob_level)
        
        # Generate loot with server-side validation
        loot_items = self.generate_loot(
            loot_table_name, 
            mob_level, 
            luck_multiplier,
            player_id=killer_id,
            source_id=mob_id
        )
        
        # Add gold drop
        gold_amount = self.calculate_gold_drop(mob_type, mob_level, luck_multiplier)
        
        if gold_amount > 0:
            self.server.add_player_gold(killer_id, gold_amount)
        
        # Create loot entities in world or add directly to inventory
        death_position = event_data['data']['deathPosition']
        
        for idx, (item_id, quantity) in enumerate(loot_items):
            # Create loot entity in world with deterministic randomness
            offset_x = self.secure_rng.uniform(-2, 2)
            offset_z = self.secure_rng.uniform(-2, 2)
            
            loot_entity_id = self.server.create_loot_entity(
                death_position[0] + offset_x,
                death_position[1],
                death_position[2] + offset_z,
                item_id,
                quantity
            )
            
            # Fire loot created event
            self.server.fire_event('loot_created', {
                'lootEntityId': loot_entity_id,
                'itemId': item_id,
                'quantity': quantity,
                'sourceMobId': mob_id,
                'position': death_position,
                'timestamp': int(time.time())
            })
        
        # Log loot generation for audit trail
        self.server.log_info(
            f"Generated {len(loot_items)} items for mob {mob_id} "
            f"killed by player {killer_id}. Gold: {gold_amount}"
        )
        
        # Record loot transaction for anti-cheat
        self.record_loot_transaction(killer_id, mob_id, loot_items, gold_amount)
        
        return True
    
    def on_chest_opened(self, event_data: Dict[str, Any]) -> bool:
        """Handle chest opening with security checks"""
        chest_id = event_data['data']['chestId']
        player_id = event_data['data']['playerId']
        chest_type = event_data['data']['chestType']
        
        # Validate player can interact with chest
        if not self.validate_player_interaction(player_id, 'chest'):
            self.server.send_message_to_player(player_id, "Cannot interact with chest.")
            return False
        
        # Check if chest has been looted recently
        if self.is_chest_on_cooldown(chest_id):
            self.server.send_message_to_player(player_id, "This chest is empty.")
            return False
        
        # Check rate limiting for chest looting
        if self.is_player_rate_limited(player_id, 'chest_loot'):
            self.server.send_message_to_player(
                player_id, 
                "You're looting too quickly. Please wait."
            )
            return False
        
        # Generate chest loot with player context
        loot_table = f"chest_{chest_type}"
        player_level = self.server.get_player_level(player_id)
        
        loot_items = self.generate_loot(
            loot_table, 
            player_level,
            player_id=player_id,
            source_id=chest_id,
            source_type='chest'
        )
        
        # Add items directly to inventory with validation
        for item_id, quantity in loot_items:
            if not self.validate_item_transaction(player_id, item_id, quantity):
                self.server.log_warning(
                    f"Suspicious item transaction blocked: "
                    f"player={player_id}, item={item_id}, qty={quantity}"
                )
                continue
                
            success = self.server.give_player_item(player_id, item_id, quantity)
            if not success:
                self.server.log_error(
                    f"Failed to give item to player {player_id}: "
                    f"{item_id}x{quantity}"
                )
        
        # Mark chest as looted
        self.mark_chest_looted(chest_id)
        
        # Record player interaction
        self.record_player_interaction(player_id, 'chest_loot', chest_id)
        
        # Send notification
        self.server.send_message_to_player(
            player_id, 
            f"You found {len(loot_items)} item(s) in the chest!"
        )
        
        return True
    
    def generate_loot(self, table_name: str, player_level: int, 
                     luck_multiplier: float = 1.0,
                     player_id: Optional[str] = None,
                     source_id: Optional[str] = None,
                     source_type: str = 'mob') -> List[tuple]:
        """Generate loot from a specific table with enhanced security"""
        # Security: Validate input parameters
        if not table_name or player_level < 1:
            self.server.log_error(f"Invalid loot generation parameters: {table_name}, {player_level}")
            return []
        
        # Security: Rate limiting check
        if player_id and self.is_player_rate_limited(player_id, 'loot_generation'):
            self.server.log_warning(f"Rate limited loot generation for player {player_id}")
            return []
        
        # Get loot table from database using parameterized query
        try:
            # Use parameterized query to prevent SQL injection
            query = "SELECT table_data FROM loot_tables WHERE table_id = ?"
            result = self.server.query_database(query, (table_name,))
            
            if not result or not result.get('success', False):
                # Fall back to cached loot tables or default
                return self.get_cached_loot_table(
                    table_name, player_level, luck_multiplier,
                    player_id, source_id, source_type
                )
            
            loot_table = result['data'][0]['table_data']
            results = []
            
            # Process guaranteed drops
            for entry in loot_table.get('guaranteed_entries', []):
                if self.meets_requirements(entry, player_level, player_id):
                    quantity = self.secure_rng.randint(
                        entry.get('minQuantity', 1),
                        entry.get('maxQuantity', 1)
                    )
                    results.append((entry['itemId'], quantity))
            
            # Process random drops
            max_drops = loot_table.get('maxDrops', 5)
            random_entries = loot_table.get('random_entries', [])
            
            # Shuffle entries for additional randomness
            shuffled_entries = random_entries.copy()
            self.secure_rng.shuffle(shuffled_entries)
            
            for entry in shuffled_entries:
                if len(results) >= max_drops:
                    break
                
                if not self.meets_requirements(entry, player_level, player_id):
                    continue
                
                # Use deterministic randomness for fairness
                if player_id and source_id:
                    # Generate deterministic random number for this specific drop
                    drop_roll = self.generate_deterministic_random(
                        player_id, source_id, len(results)
                    )
                else:
                    # Fallback to secure random
                    drop_roll = self.secure_rng.random()
                
                drop_chance = entry.get('dropChance', 0.0) * luck_multiplier
                if drop_roll <= drop_chance:
                    quantity = self.secure_rng.randint(
                        entry.get('minQuantity', 1),
                        entry.get('maxQuantity', 1)
                    )
                    results.append((entry['itemId'], quantity))
            
            return results
            
        except Exception as e:
            self.server.log_error(f"Error generating loot: {str(e)}")
            return []
    
    def get_cached_loot_table(self, table_name: str, player_level: int, 
                             luck_multiplier: float,
                             player_id: Optional[str] = None,
                             source_id: Optional[str] = None,
                             source_type: str = 'mob') -> List[tuple]:
        """Fallback method using cached loot tables with security"""
        # Try to load from cache first
        if table_name in self.loot_tables:
            return self.generate_loot_from_cache(
                table_name, player_level, luck_multiplier,
                player_id, source_id, source_type
            )
        
        # Load from file as last resort
        try:
            import json
            with open(f'data/loot_tables/{table_name}.json', 'r') as f:
                loot_table = json.load(f)
                self.loot_tables[table_name] = loot_table
                return self.generate_loot_from_cache(
                    table_name, player_level, luck_multiplier,
                    player_id, source_id, source_type
                )
        except Exception as e:
            self.server.log_error(f"Failed to load loot table {table_name}: {str(e)}")
            return []
    
    def generate_loot_from_cache(self, table_name: str, player_level: int,
                                luck_multiplier: float,
                                player_id: Optional[str] = None,
                                source_id: Optional[str] = None,
                                source_type: str = 'mob') -> List[tuple]:
        """Generate loot from cached table data with security"""
        loot_table = self.loot_tables[table_name]
        results = []
        
        # Process guaranteed drops
        for entry in loot_table.get('guaranteed_entries', []):
            if self.meets_requirements(entry, player_level, player_id):
                quantity = self.secure_rng.randint(
                    entry.get('minQuantity', 1),
                    entry.get('maxQuantity', 1)
                )
                results.append((entry['itemId'], quantity))
        
        # Process random drops
        max_drops = loot_table.get('maxDrops', 5)
        random_entries = loot_table.get('random_entries', [])
        
        # Shuffle for additional randomness
        shuffled_entries = random_entries.copy()
        self.secure_rng.shuffle(shuffled_entries)
        
        for entry in shuffled_entries:
            if len(results) >= max_drops:
                break
            
            if not self.meets_requirements(entry, player_level, player_id):
                continue
            
            # Use appropriate random method
            if player_id and source_id:
                drop_roll = self.generate_deterministic_random(
                    player_id, source_id, len(results)
                )
            else:
                drop_roll = self.secure_rng.random()
            
            drop_chance = entry.get('dropChance', 0.0) * luck_multiplier
            if drop_roll <= drop_chance:
                quantity = self.secure_rng.randint(
                    entry.get('minQuantity', 1),
                    entry.get('maxQuantity', 1)
                )
                results.append((entry['itemId'], quantity))
        
        return results
    
    def meets_requirements(self, entry: Dict[str, Any], 
                          player_level: int, 
                          player_id: Optional[str] = None) -> bool:
        """Check if player meets loot entry requirements with validation"""
        if player_level < entry.get('minLevel', 1):
            return False
        
        if player_level > entry.get('maxLevel', 100):
            return False
        
        # Check quest requirements if player_id provided
        required_quest = entry.get('requiredQuest')
        if required_quest and player_id:
            # Use parameterized query
            query = """
                SELECT 1 FROM player_quests 
                WHERE player_id = ? AND quest_id = ? AND completed = true
            """
            result = self.server.query_database(query, (player_id, required_quest))
            if not result or not result.get('success', False) or not result.get('data'):
                return False
        
        return True
    
    def get_mob_loot_table(self, mob_type: int, mob_level: int) -> str:
        """Get appropriate loot table for mob type and level"""
        mob_types = {
            0: "goblin",  # GOBLIN
            1: "orc",     # ORC
            2: "dragon",  # DRAGON
            3: "slime"    # SLIME
        }
        
        base_table = mob_types.get(mob_type, "default")
        
        # Adjust table based on level
        if mob_level >= 20:
            return f"{base_table}_elite"
        elif mob_level >= 10:
            return f"{base_table}_advanced"
        else:
            return base_table
    
    def calculate_gold_drop(self, mob_type: int, mob_level: int, 
                           luck_multiplier: float) -> int:
        """Calculate gold drop from mob with secure randomness"""
        base_gold = mob_level * 10
        
        # Type multipliers
        type_multipliers = {
            0: 0.8,   # GOBLIN
            1: 1.2,   # ORC
            2: 5.0,   # DRAGON
            3: 0.5    # SLIME
        }
        
        multiplier = type_multipliers.get(mob_type, 1.0)
        gold = int(base_gold * multiplier * luck_multiplier)
        
        # Add secure random variation
        variation = self.secure_rng.randint(-gold // 4, gold // 4)
        gold += variation
        
        return max(1, gold)
    
    def is_chest_on_cooldown(self, chest_id: str) -> bool:
        """Check if chest is on respawn cooldown"""
        # Use parameterized query
        query = "SELECT loot_time FROM chest_cooldowns WHERE chest_id = ?"
        result = self.server.query_database(query, (chest_id,))
        
        if not result or not result.get('success', False):
            return False
            
        if result.get('data'):
            loot_time = result['data'][0]['loot_time']
            current_time = int(time.time())
            respawn_time = 300  # 5 minutes default
            
            return (current_time - loot_time) < respawn_time
        
        return False
    
    def mark_chest_looted(self, chest_id: str):
        """Mark chest as looted with timestamp"""
        current_time = int(time.time())
        
        # Use parameterized query
        query = """
            INSERT INTO chest_cooldowns (chest_id, loot_time) 
            VALUES (?, ?) 
            ON CONFLICT (chest_id) 
            DO UPDATE SET loot_time = ?
        """
        self.server.execute_database(query, (chest_id, current_time, current_time))
    
    # Security validation methods
    def validate_player_can_receive_loot(self, player_id: str) -> bool:
        """Validate player is allowed to receive loot"""
        # Check if player exists and is active
        if not self.server.is_player_online(player_id):
            return False
        
        # Check if player is banned or restricted
        query = "SELECT status FROM players WHERE player_id = ?"
        result = self.server.query_database(query, (player_id,))
        
        if result and result.get('success', False) and result.get('data'):
            player_status = result['data'][0].get('status', 'active')
            if player_status != 'active':
                return False
        
        # Additional anti-cheat checks could go here
        return True
    
    def validate_player_interaction(self, player_id: str, interaction_type: str) -> bool:
        """Validate player can perform specific interaction"""
        # Basic validation - can be expanded
        return self.validate_player_can_receive_loot(player_id)
    
    def is_player_rate_limited(self, player_id: str, action_type: str) -> bool:
        """Check if player is rate limited for specific action"""
        # Simple rate limiting - implement based on your needs
        cache_key = f"rate_limit:{player_id}:{action_type}"
        current_count = self.server.get_cache(cache_key, 0)
        
        # Example: Max 10 loot actions per minute
        if current_count >= 10:
            return True
        
        # Increment counter with expiry
        self.server.set_cache(cache_key, current_count + 1, expire=60)
        return False
    
    def validate_item_transaction(self, player_id: str, item_id: str, quantity: int) -> bool:
        """Validate item transaction for potential cheating"""
        # Check for reasonable quantities
        if quantity <= 0 or quantity > 1000:  # Adjust max as needed
            return False
        
        # Check item exists
        query = "SELECT 1 FROM items WHERE item_id = ?"
        result = self.server.query_database(query, (item_id,))
        if not result or not result.get('success', False) or not result.get('data'):
            return False
        
        # Additional validation logic here
        return True
    
    def record_loot_transaction(self, player_id: str, source_id: str, 
                               loot_items: List[tuple], gold_amount: int):
        """Record loot transaction for audit/anti-cheat purposes"""
        try:
            query = """
                INSERT INTO loot_transactions 
                (player_id, source_id, loot_data, gold_amount, timestamp) 
                VALUES (?, ?, ?, ?, ?)
            """
            loot_data = str(loot_items)  # Convert to string or JSON
            timestamp = int(time.time())
            
            self.server.execute_database(
                query, 
                (player_id, source_id, loot_data, gold_amount, timestamp)
            )
        except Exception as e:
            self.server.log_error(f"Failed to record loot transaction: {str(e)}")
    
    def record_player_interaction(self, player_id: str, interaction_type: str, 
                                 target_id: str):
        """Record player interaction for analytics/anti-cheat"""
        try:
            query = """
                INSERT INTO player_interactions 
                (player_id, interaction_type, target_id, timestamp) 
                VALUES (?, ?, ?, ?)
            """
            timestamp = int(time.time())
            
            self.server.execute_database(
                query, 
                (player_id, interaction_type, target_id, timestamp)
            )
        except Exception as e:
            self.server.log_error(f"Failed to record player interaction: {str(e)}")
    
    def on_quest_completed(self, event_data: Dict[str, Any]) -> bool:
        """Handle quest completion rewards"""
        # Implementation for quest rewards
        player_id = event_data['data']['playerId']
        quest_id = event_data['data']['questId']
        
        # Generate quest rewards
        # Similar pattern to mob/chest loot
        
        return True
    
    def on_player_level_up(self, event_data: Dict[str, Any]) -> bool:
        """Handle player level up rewards"""
        # Implementation for level up rewards
        player_id = event_data['data']['playerId']
        new_level = event_data['data']['newLevel']
        
        # Grant level up rewards
        # Similar pattern to other loot generation
        
        return True


def register_event_handlers(server):
    """Main registration function called by server"""
    handler = LootHandler(server)
    handler.register_event_handlers()
    return handler