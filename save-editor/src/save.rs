//! Typed model over the PE2 save image (`McSaveState`).
//!
//! The model owns the raw 0x200-byte save record and exposes typed
//! accessors at the struct's field offsets. Unknown bytes are never touched:
//! edits go through `get`/`set` helpers, and JSON transfer serializes only
//! the typed fields, so re-importing onto an existing record preserves
//! everything the editor does not model.

use anyhow::{bail, Result};
use serde::{Deserialize, Serialize};

/// Save image length (`sizeof(McSaveData)` = checksum header + 0x940 payload).
pub const SAVE_IMAGE_SIZE: usize = 0x944;
/// Length of the checksummed payload (`saveChecksum` covers 0x940 bytes).
pub const SAVE_PAYLOAD_SIZE: usize = 0x940;
/// Row count of the save's own item table (`McSaveState::itemRows`).
pub const ITEM_ROW_COUNT: usize = 0x100;
/// Weapon consumable load record count, indexed by weapon id - 0x80.
pub const WEAPON_LOAD_COUNT: usize = 0x20;
/// Parasite Energy attachment level byte count.
pub const PE_LEVEL_COUNT: usize = 0x12;
/// Parasite Energy use count entries.
pub const PE_USE_COUNT: usize = 19;
/// Weapon use count entries.
pub const WEAPON_USE_COUNT: usize = 0x20;
/// Item level bonus entries (items 0x60..0x7F).
pub const ITEM_LEVEL_BONUS_COUNT: usize = 0x20;
/// Collectible bit words (4 x 32 = 128 collectibles).
pub const COLLECTED_BIT_WORDS: usize = 4;
/// Item-seen bit words (0x60 x 32 = 0x180 item ids).
pub const ITEM_SEEN_BIT_WORDS: usize = 0x60;

// McSaveState field offsets (little-endian, verified against static asserts).
pub const OFF_CHECKSUM: usize = 0x00;
pub const OFF_LOCATION: usize = 0x04; // GameLocationKey, 8 bytes
pub const OFF_PLAY_TIME: usize = 0x0C; // u16 minutes, cap 59999
pub const OFF_CLEAR_COUNT: usize = 0x0E; // s8
pub const OFF_GAME_MODE: usize = 0x0F; // s8 0..3
pub const OFF_VISIT_FLAGS: usize = 0x10; // u8 bits 0..5
pub const OFF_SAVE_NUMBER: usize = 0x11; // s8 1..99
pub const OFF_SAVE_POINT: usize = 0x12; // u8 1..16
pub const OFF_COMPANION_TYPE: usize = 0x13; // s8 0..3
pub const OFF_PLAYER_EXP: usize = 0x14; // s32
pub const OFF_PLAYER_BP: usize = 0x18; // s32
pub const OFF_HEADER_CHECKSUM: usize = 0x1C;
pub const OFF_VIBRATION: usize = 0x21;
pub const OFF_CHARACTER_ID: usize = 0x22;
pub const OFF_DEMO_SCENE: usize = 0x23;
pub const OFF_MOVE_MODE: usize = 0x25;
pub const OFF_HP_BONUS: usize = 0x26;
pub const OFF_MP_BONUS: usize = 0x27;
pub const OFF_ENEMY_POSES: usize = 0x28; // 0x20 x 12 bytes
pub const OFF_BUTTON_LAYOUT: usize = 0x1A8;
pub const OFF_SOUND_MODE: usize = 0x1A9;
pub const OFF_MUSIC_VOLUME: usize = 0x1AA;
pub const OFF_CURSOR_MODE: usize = 0x1AB;
pub const OFF_ITEM_ROWS: usize = 0x1AC; // 0x100 x 4 bytes
pub const OFF_COLLECTED_BITS: usize = 0x5AC; // 4 x u32
pub const OFF_CARRIED_ITEMS: usize = 0x5BC; // InventoryItemRange, 4 bytes
pub const OFF_ACTORS_FROZEN: usize = 0x5C1;
pub const OFF_CHEAT_MODE: usize = 0x5C2;
pub const OFF_INTERLACE: usize = 0x5C3;
pub const OFF_SCENE_EVENT: usize = 0x5C5;
pub const OFF_COMPANION_VARIANT: usize = 0x5C7;
pub const OFF_WEAPON_ITEMS: usize = 0x5C8; // 0x20 x 8 bytes
pub const OFF_COMPANION_HP: usize = 0x6C8; // s16
pub const OFF_COMPANION_HP_MAX: usize = 0x6CA; // s16
pub const OFF_BATTLES_WON: usize = 0x6CC; // u16 cap 9999
pub const OFF_BATTLES_ESCAPED: usize = 0x6CE; // u16 cap 9999
pub const OFF_ITEM_SEEN_BITS: usize = 0x6D0; // 0x60 x u32
pub const OFF_ATTACH_LEVELS: usize = 0x850; // 0x12 bytes
pub const OFF_ATTACH_USE_COUNTS: usize = 0x862; // 19 x s16
pub const OFF_WEAPON_USE_COUNTS: usize = 0x888; // 0x20 x s32
pub const OFF_ITEM_LEVEL_BONUS: usize = 0x908; // 0x20 bytes
pub const OFF_REPLAY_RANK: usize = 0x92A;
pub const OFF_SAVE_COUNT: usize = 0x92B;
pub const OFF_MAX_EXP: usize = 0x92C; // s32
pub const OFF_MAX_BP: usize = 0x930; // s32
pub const OFF_SHOP_TIERS: usize = 0x934; // u32 bitmask, 13 tiers
pub const OFF_SHOP_STOCK: usize = 0x938; // u32, 12 two-bit levels
pub const OFF_TITLE_CHECKSUM: usize = 0x93C;
pub const OFF_BUFFER_CHECKSUM: usize = 0x940;

/// Place label for a save point (1..16); index 0 is unused and 15 is Opening.
pub const SAVE_POINT_LABELS: [&str; 17] = [
    "Where am I?",
    "Square",
    "Fire Escape",
    "Mist Parking",
    "Gas Station",
    "Trailer Coach",
    "Motel Lobby",
    "Refuge",
    "Sterilization Room",
    "Underground Parking",
    "Laboratory",
    "Incinerator Control Room",
    "Pod Deck",
    "Nursery",
    "Tent",
    "Opening",
    "Motel Room 6",
];

/// Run mode (`McSaveState::gameMode`).
pub const GAME_MODE_LABELS: [&str; 4] = ["Replay", "Bounty", "Scavenger", "Nightmare"];

/// One inventory row (`InventoryItemRow`), little-endian on card.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct ItemRow {
    pub item_id: u8,
    /// Armour attachment position: 0 none, 1..10 attached, -1 equipped armour.
    pub attach_slot: i8,
    /// Total units, including ammunition loaded into weapons.
    pub qty: u16,
}

/// Weapon consumable loads (`EquipmentWeaponLoad`), one per weapon id 0x80..0x9F.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct WeaponLoad {
    pub primary_item_id: u8,
    pub primary_qty: u8,
    pub secondary_item_id: u8,
    pub secondary_qty: u8,
    /// Preserved verbatim; role unproven in the decompilation.
    pub field_4: i32,
}

/// Carried-item window (`InventoryItemRange`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct CarriedRange {
    pub first_row: u8,
    pub row_count: u8,
    pub table_id: u8,
    pub field_3: u8,
}

/// Typed view over one save image. Unknown and unmodeled bytes stay untouched.
#[derive(Debug, Clone)]
pub struct SaveImage {
    /// The full 0x944-byte record, checksum header included.
    pub raw: Vec<u8>,
}

impl SaveImage {
    /// Wrap an existing image, validating its size.
    pub fn from_bytes(raw: Vec<u8>) -> Result<Self> {
        if raw.len() != SAVE_IMAGE_SIZE {
            bail!(
                "save image must be {} bytes, got {}",
                SAVE_IMAGE_SIZE,
                raw.len()
            );
        }
        Ok(Self { raw })
    }

    /// A zeroed image with valid structure (no checksums yet).
    pub fn blank() -> Self {
        Self {
            raw: vec![0; SAVE_IMAGE_SIZE],
        }
    }

    // -- little-endian field helpers -------------------------------------------------

    fn u16_at(&self, off: usize) -> u16 {
        u16::from_le_bytes([self.raw[off], self.raw[off + 1]])
    }
    fn set_u16_at(&mut self, off: usize, v: u16) {
        self.raw[off..off + 2].copy_from_slice(&v.to_le_bytes());
    }
    fn s16_at(&self, off: usize) -> i16 {
        i16::from_le_bytes([self.raw[off], self.raw[off + 1]])
    }
    fn set_s16_at(&mut self, off: usize, v: i16) {
        self.raw[off..off + 2].copy_from_slice(&v.to_le_bytes());
    }
    fn u32_at(&self, off: usize) -> u32 {
        u32::from_le_bytes(self.raw[off..off + 4].try_into().unwrap())
    }
    fn set_u32_at(&mut self, off: usize, v: u32) {
        self.raw[off..off + 4].copy_from_slice(&v.to_le_bytes());
    }
    fn i32_at(&self, off: usize) -> i32 {
        i32::from_le_bytes(self.raw[off..off + 4].try_into().unwrap())
    }
    fn set_i32_at(&mut self, off: usize, v: i32) {
        self.raw[off..off + 4].copy_from_slice(&v.to_le_bytes());
    }

    // -- scalars ---------------------------------------------------------------------

    pub fn play_time_minutes(&self) -> u16 {
        self.u16_at(OFF_PLAY_TIME)
    }
    pub fn set_play_time_minutes(&mut self, v: u16) {
        self.set_u16_at(OFF_PLAY_TIME, v.min(59999));
    }
    pub fn clear_count(&self) -> i8 {
        self.raw[OFF_CLEAR_COUNT] as i8
    }
    pub fn set_clear_count(&mut self, v: i8) {
        self.raw[OFF_CLEAR_COUNT] = (v.clamp(0, 99)) as u8;
    }
    pub fn game_mode(&self) -> u8 {
        self.raw[OFF_GAME_MODE]
    }
    pub fn set_game_mode(&mut self, v: u8) {
        self.raw[OFF_GAME_MODE] = v.min(3);
    }
    pub fn visit_flags(&self) -> u8 {
        self.raw[OFF_VISIT_FLAGS]
    }
    pub fn set_visit_flags(&mut self, v: u8) {
        self.raw[OFF_VISIT_FLAGS] = v & 0x3F;
    }
    pub fn save_number(&self) -> i8 {
        self.raw[OFF_SAVE_NUMBER] as i8
    }
    pub fn set_save_number(&mut self, v: i8) {
        self.raw[OFF_SAVE_NUMBER] = (v.clamp(1, 99)) as u8;
    }
    pub fn save_point(&self) -> u8 {
        self.raw[OFF_SAVE_POINT]
    }
    pub fn set_save_point(&mut self, v: u8) {
        self.raw[OFF_SAVE_POINT] = v.clamp(1, 16);
    }
    pub fn save_point_label(&self) -> &'static str {
        SAVE_POINT_LABELS[self.save_point() as usize]
    }
    pub fn companion_type(&self) -> i8 {
        self.raw[OFF_COMPANION_TYPE] as i8
    }
    pub fn set_companion_type(&mut self, v: i8) {
        self.raw[OFF_COMPANION_TYPE] = (v.clamp(0, 3)) as u8;
    }
    pub fn player_exp(&self) -> i32 {
        self.i32_at(OFF_PLAYER_EXP)
    }
    pub fn set_player_exp(&mut self, v: i32) {
        self.set_i32_at(OFF_PLAYER_EXP, v);
    }
    pub fn player_bp(&self) -> i32 {
        self.i32_at(OFF_PLAYER_BP)
    }
    pub fn set_player_bp(&mut self, v: i32) {
        self.set_i32_at(OFF_PLAYER_BP, v);
    }
    pub fn max_exp(&self) -> i32 {
        self.i32_at(OFF_MAX_EXP)
    }
    pub fn set_max_exp(&mut self, v: i32) {
        self.set_i32_at(OFF_MAX_EXP, v);
    }
    pub fn max_bp(&self) -> i32 {
        self.i32_at(OFF_MAX_BP)
    }
    pub fn set_max_bp(&mut self, v: i32) {
        self.set_i32_at(OFF_MAX_BP, v);
    }
    pub fn hp_bonus(&self) -> u8 {
        self.raw[OFF_HP_BONUS]
    }
    pub fn set_hp_bonus(&mut self, v: u8) {
        self.raw[OFF_HP_BONUS] = v;
    }
    pub fn mp_bonus(&self) -> u8 {
        self.raw[OFF_MP_BONUS]
    }
    pub fn set_mp_bonus(&mut self, v: u8) {
        self.raw[OFF_MP_BONUS] = v;
    }
    pub fn character_id(&self) -> i8 {
        self.raw[OFF_CHARACTER_ID] as i8
    }
    pub fn set_character_id(&mut self, v: i8) {
        self.raw[OFF_CHARACTER_ID] = v as u8;
    }
    pub fn companion_hp(&self) -> i16 {
        self.s16_at(OFF_COMPANION_HP)
    }
    pub fn set_companion_hp(&mut self, v: i16) {
        self.set_s16_at(OFF_COMPANION_HP, v);
    }
    pub fn companion_hp_max(&self) -> i16 {
        self.s16_at(OFF_COMPANION_HP_MAX)
    }
    pub fn set_companion_hp_max(&mut self, v: i16) {
        self.set_s16_at(OFF_COMPANION_HP_MAX, v);
    }
    pub fn companion_variant(&self) -> i8 {
        self.raw[OFF_COMPANION_VARIANT] as i8
    }
    pub fn set_companion_variant(&mut self, v: i8) {
        self.raw[OFF_COMPANION_VARIANT] = v as u8;
    }
    pub fn battles_won(&self) -> u16 {
        self.u16_at(OFF_BATTLES_WON)
    }
    pub fn set_battles_won(&mut self, v: u16) {
        self.set_u16_at(OFF_BATTLES_WON, v.min(9999));
    }
    pub fn battles_escaped(&self) -> u16 {
        self.u16_at(OFF_BATTLES_ESCAPED)
    }
    pub fn set_battles_escaped(&mut self, v: u16) {
        self.set_u16_at(OFF_BATTLES_ESCAPED, v.min(9999));
    }
    pub fn save_count(&self) -> u8 {
        self.raw[OFF_SAVE_COUNT]
    }
    pub fn set_save_count(&mut self, v: u8) {
        self.raw[OFF_SAVE_COUNT] = v.min(99);
    }
    pub fn replay_rank(&self) -> i8 {
        self.raw[OFF_REPLAY_RANK] as i8
    }
    pub fn set_replay_rank(&mut self, v: i8) {
        self.raw[OFF_REPLAY_RANK] = (v.clamp(0, 2)) as u8;
    }
    pub fn shop_tiers(&self) -> u32 {
        self.u32_at(OFF_SHOP_TIERS)
    }
    pub fn set_shop_tiers(&mut self, v: u32) {
        self.set_u32_at(OFF_SHOP_TIERS, v & 0x1FFF);
    }
    pub fn shop_stock(&self) -> u32 {
        self.u32_at(OFF_SHOP_STOCK)
    }
    pub fn set_shop_stock(&mut self, v: u32) {
        self.set_u32_at(OFF_SHOP_STOCK, v);
    }
    pub fn location(&self) -> [u8; 8] {
        self.raw[OFF_LOCATION..OFF_LOCATION + 8].try_into().unwrap()
    }
    pub fn set_location(&mut self, v: [u8; 8]) {
        self.raw[OFF_LOCATION..OFF_LOCATION + 8].copy_from_slice(&v);
    }

    // -- options ---------------------------------------------------------------------

    pub fn vibration_off(&self) -> bool {
        self.raw[OFF_VIBRATION] != 0
    }
    pub fn set_vibration_off(&mut self, off: bool) {
        self.raw[OFF_VIBRATION] = off as u8;
    }
    pub fn run_default(&self) -> bool {
        self.raw[OFF_MOVE_MODE] != 0
    }
    pub fn set_run_default(&mut self, run: bool) {
        self.raw[OFF_MOVE_MODE] = run as u8;
    }
    pub fn button_layout(&self) -> u8 {
        self.raw[OFF_BUTTON_LAYOUT]
    }
    pub fn set_button_layout(&mut self, v: u8) {
        self.raw[OFF_BUTTON_LAYOUT] = v.min(2);
    }
    pub fn mono_sound(&self) -> bool {
        self.raw[OFF_SOUND_MODE] != 0
    }
    pub fn set_mono_sound(&mut self, mono: bool) {
        self.raw[OFF_SOUND_MODE] = mono as u8;
    }
    pub fn music_volume(&self) -> u8 {
        self.raw[OFF_MUSIC_VOLUME]
    }
    pub fn set_music_volume(&mut self, v: u8) {
        self.raw[OFF_MUSIC_VOLUME] = v.min(3);
    }
    pub fn cursor_reset(&self) -> bool {
        self.raw[OFF_CURSOR_MODE] != 0
    }
    pub fn set_cursor_reset(&mut self, reset: bool) {
        self.raw[OFF_CURSOR_MODE] = reset as u8;
    }
    pub fn interlaced(&self) -> bool {
        self.raw[OFF_INTERLACE] != 0
    }
    pub fn set_interlaced(&mut self, v: bool) {
        self.raw[OFF_INTERLACE] = v as u8;
    }
    pub fn cheat_mode(&self) -> bool {
        self.raw[OFF_CHEAT_MODE] != 0
    }
    pub fn set_cheat_mode(&mut self, v: bool) {
        self.raw[OFF_CHEAT_MODE] = v as u8;
    }
    pub fn actors_frozen(&self) -> bool {
        self.raw[OFF_ACTORS_FROZEN] != 0
    }
    pub fn set_actors_frozen(&mut self, v: bool) {
        self.raw[OFF_ACTORS_FROZEN] = v as u8;
    }

    // -- inventory -------------------------------------------------------------------

    /// Read one row of the save's item table.
    pub fn item_row(&self, index: usize) -> ItemRow {
        assert!(index < ITEM_ROW_COUNT);
        let off = OFF_ITEM_ROWS + index * 4;
        ItemRow {
            item_id: self.raw[off],
            attach_slot: self.raw[off + 1] as i8,
            qty: self.u16_at(off + 2),
        }
    }
    /// Write one row of the save's item table.
    pub fn set_item_row(&mut self, index: usize, row: ItemRow) {
        assert!(index < ITEM_ROW_COUNT);
        let off = OFF_ITEM_ROWS + index * 4;
        self.raw[off] = row.item_id;
        self.raw[off + 1] = row.attach_slot as u8;
        self.set_u16_at(off + 2, row.qty);
    }
    /// The carried-items window descriptor.
    pub fn carried_range(&self) -> CarriedRange {
        let off = OFF_CARRIED_ITEMS;
        CarriedRange {
            first_row: self.raw[off],
            row_count: self.raw[off + 1],
            table_id: self.raw[off + 2],
            field_3: self.raw[off + 3],
        }
    }
    pub fn set_carried_range(&mut self, r: CarriedRange) {
        let off = OFF_CARRIED_ITEMS;
        self.raw[off] = r.first_row;
        self.raw[off + 1] = r.row_count;
        self.raw[off + 2] = r.table_id;
        self.raw[off + 3] = r.field_3;
    }

    // -- weapon consumable loads -----------------------------------------------------

    pub fn weapon_load(&self, index: usize) -> WeaponLoad {
        assert!(index < WEAPON_LOAD_COUNT);
        let off = OFF_WEAPON_ITEMS + index * 8;
        WeaponLoad {
            primary_item_id: self.raw[off],
            primary_qty: self.raw[off + 1],
            secondary_item_id: self.raw[off + 2],
            secondary_qty: self.raw[off + 3],
            field_4: self.i32_at(off + 4),
        }
    }
    pub fn set_weapon_load(&mut self, index: usize, load: WeaponLoad) {
        assert!(index < WEAPON_LOAD_COUNT);
        let off = OFF_WEAPON_ITEMS + index * 8;
        self.raw[off] = load.primary_item_id;
        self.raw[off + 1] = load.primary_qty;
        self.raw[off + 2] = load.secondary_item_id;
        self.raw[off + 3] = load.secondary_qty;
        self.set_i32_at(off + 4, load.field_4);
    }
    pub fn weapon_use_count(&self, index: usize) -> i32 {
        assert!(index < WEAPON_USE_COUNT);
        self.i32_at(OFF_WEAPON_USE_COUNTS + index * 4)
    }
    pub fn set_weapon_use_count(&mut self, index: usize, v: i32) {
        assert!(index < WEAPON_USE_COUNT);
        self.set_i32_at(OFF_WEAPON_USE_COUNTS + index * 4, v);
    }

    // -- parasite energy -------------------------------------------------------------

    pub fn pe_level(&self, index: usize) -> u8 {
        assert!(index < PE_LEVEL_COUNT);
        self.raw[OFF_ATTACH_LEVELS + index].min(3)
    }
    pub fn set_pe_level(&mut self, index: usize, v: u8) {
        assert!(index < PE_LEVEL_COUNT);
        self.raw[OFF_ATTACH_LEVELS + index] = v.min(3);
    }
    pub fn pe_use_count(&self, index: usize) -> i16 {
        assert!(index < PE_USE_COUNT);
        self.s16_at(OFF_ATTACH_USE_COUNTS + index * 2)
    }
    pub fn set_pe_use_count(&mut self, index: usize, v: i16) {
        assert!(index < PE_USE_COUNT);
        self.set_s16_at(OFF_ATTACH_USE_COUNTS + index * 2, v);
    }

    // -- item level bonus ------------------------------------------------------------

    pub fn item_level_bonus(&self, index: usize) -> u8 {
        assert!(index < ITEM_LEVEL_BONUS_COUNT);
        self.raw[OFF_ITEM_LEVEL_BONUS + index]
    }
    pub fn set_item_level_bonus(&mut self, index: usize, v: u8) {
        assert!(index < ITEM_LEVEL_BONUS_COUNT);
        self.raw[OFF_ITEM_LEVEL_BONUS + index] = v;
    }

    // -- bit fields ------------------------------------------------------------------

    pub fn collected_bit(&self, bit: usize) -> bool {
        assert!(bit < COLLECTED_BIT_WORDS * 32);
        self.u32_at(OFF_COLLECTED_BITS + (bit / 32) * 4) & (1 << (bit % 32)) != 0
    }
    pub fn set_collected_bit(&mut self, bit: usize, on: bool) {
        assert!(bit < COLLECTED_BIT_WORDS * 32);
        let off = OFF_COLLECTED_BITS + (bit / 32) * 4;
        let word = self.u32_at(off);
        self.set_u32_at(
            off,
            if on {
                word | (1 << (bit % 32))
            } else {
                word & !(1 << (bit % 32))
            },
        );
    }
    pub fn item_seen_bit(&self, item_id: usize) -> bool {
        assert!(item_id < ITEM_SEEN_BIT_WORDS * 32);
        self.u32_at(OFF_ITEM_SEEN_BITS + (item_id / 32) * 4) & (1 << (item_id % 32)) != 0
    }
    pub fn set_item_seen_bit(&mut self, item_id: usize, on: bool) {
        assert!(item_id < ITEM_SEEN_BIT_WORDS * 32);
        let off = OFF_ITEM_SEEN_BITS + (item_id / 32) * 4;
        let word = self.u32_at(off);
        self.set_u32_at(
            off,
            if on {
                word | (1 << (item_id % 32))
            } else {
                word & !(1 << (item_id % 32))
            },
        );
    }

    /// Mark every item id up to `max_id` (exclusive) as seen.
    pub fn mark_items_seen(&mut self, max_id: usize) {
        for id in 0..max_id.min(ITEM_SEEN_BIT_WORDS * 32) {
            self.set_item_seen_bit(id, true);
        }
    }
}

/// Serializable snapshot of the typed fields for JSON transfer.
///
/// Unmodeled bytes are intentionally absent: importing merges this snapshot
/// onto an existing image and leaves everything else untouched.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct SaveTransfer {
    pub location: Option<[u8; 8]>,
    pub play_time_minutes: Option<u16>,
    pub clear_count: Option<i8>,
    pub game_mode: Option<u8>,
    pub visit_flags: Option<u8>,
    pub save_number: Option<i8>,
    pub save_point: Option<u8>,
    pub companion_type: Option<i8>,
    pub companion_variant: Option<i8>,
    pub companion_hp: Option<i16>,
    pub companion_hp_max: Option<i16>,
    pub player_exp: Option<i32>,
    pub player_bp: Option<i32>,
    pub max_exp: Option<i32>,
    pub max_bp: Option<i32>,
    pub hp_bonus: Option<u8>,
    pub mp_bonus: Option<u8>,
    pub character_id: Option<i8>,
    pub battles_won: Option<u16>,
    pub battles_escaped: Option<u16>,
    pub save_count: Option<u8>,
    pub replay_rank: Option<i8>,
    pub shop_tiers: Option<u32>,
    pub shop_stock: Option<u32>,
    pub vibration_off: Option<bool>,
    pub run_default: Option<bool>,
    pub button_layout: Option<u8>,
    pub mono_sound: Option<bool>,
    pub music_volume: Option<u8>,
    pub cursor_reset: Option<bool>,
    pub interlaced: Option<bool>,
    pub cheat_mode: Option<bool>,
    pub actors_frozen: Option<bool>,
    pub item_rows: Option<Vec<Option<ItemRow>>>,
    pub carried_range: Option<CarriedRange>,
    pub weapon_loads: Option<Vec<Option<WeaponLoad>>>,
    pub weapon_use_counts: Option<Vec<i32>>,
    pub pe_levels: Option<Vec<u8>>,
    pub pe_use_counts: Option<Vec<i16>>,
    pub item_level_bonus: Option<Vec<u8>>,
    pub collected_bits: Option<Vec<usize>>,
    pub item_seen_bits: Option<Vec<usize>>,
}

impl SaveImage {
    /// Export the typed fields into a transfer snapshot.
    pub fn to_transfer(&self) -> SaveTransfer {
        SaveTransfer {
            location: Some(self.location()),
            play_time_minutes: Some(self.play_time_minutes()),
            clear_count: Some(self.clear_count()),
            game_mode: Some(self.game_mode()),
            visit_flags: Some(self.visit_flags()),
            save_number: Some(self.save_number()),
            save_point: Some(self.save_point()),
            companion_type: Some(self.companion_type()),
            companion_variant: Some(self.companion_variant()),
            companion_hp: Some(self.companion_hp()),
            companion_hp_max: Some(self.companion_hp_max()),
            player_exp: Some(self.player_exp()),
            player_bp: Some(self.player_bp()),
            max_exp: Some(self.max_exp()),
            max_bp: Some(self.max_bp()),
            hp_bonus: Some(self.hp_bonus()),
            mp_bonus: Some(self.mp_bonus()),
            character_id: Some(self.character_id()),
            battles_won: Some(self.battles_won()),
            battles_escaped: Some(self.battles_escaped()),
            save_count: Some(self.save_count()),
            replay_rank: Some(self.replay_rank()),
            shop_tiers: Some(self.shop_tiers()),
            shop_stock: Some(self.shop_stock()),
            vibration_off: Some(self.vibration_off()),
            run_default: Some(self.run_default()),
            button_layout: Some(self.button_layout()),
            mono_sound: Some(self.mono_sound()),
            music_volume: Some(self.music_volume()),
            cursor_reset: Some(self.cursor_reset()),
            interlaced: Some(self.interlaced()),
            cheat_mode: Some(self.cheat_mode()),
            actors_frozen: Some(self.actors_frozen()),
            item_rows: Some(
                (0..ITEM_ROW_COUNT)
                    .map(|i| {
                        let row = self.item_row(i);
                        if row.item_id == 0 {
                            None
                        } else {
                            Some(row)
                        }
                    })
                    .collect(),
            ),
            carried_range: Some(self.carried_range()),
            weapon_loads: Some(
                (0..WEAPON_LOAD_COUNT)
                    .map(|i| Some(self.weapon_load(i)))
                    .collect(),
            ),
            weapon_use_counts: Some(
                (0..WEAPON_USE_COUNT)
                    .map(|i| self.weapon_use_count(i))
                    .collect(),
            ),
            pe_levels: Some((0..PE_LEVEL_COUNT).map(|i| self.pe_level(i)).collect()),
            pe_use_counts: Some((0..PE_USE_COUNT).map(|i| self.pe_use_count(i)).collect()),
            item_level_bonus: Some(
                (0..ITEM_LEVEL_BONUS_COUNT)
                    .map(|i| self.item_level_bonus(i))
                    .collect(),
            ),
            collected_bits: Some(
                (0..COLLECTED_BIT_WORDS * 32)
                    .filter(|b| self.collected_bit(*b))
                    .collect(),
            ),
            item_seen_bits: Some(
                (0..ITEM_SEEN_BIT_WORDS * 32)
                    .filter(|b| self.item_seen_bit(*b))
                    .collect(),
            ),
        }
    }

    /// Apply a transfer snapshot onto this image. `None` fields are skipped,
    /// so partial snapshots edit only the fields they carry.
    pub fn apply_transfer(&mut self, t: &SaveTransfer) {
        if let Some(v) = t.location {
            self.set_location(v);
        }
        if let Some(v) = t.play_time_minutes {
            self.set_play_time_minutes(v);
        }
        if let Some(v) = t.clear_count {
            self.set_clear_count(v);
        }
        if let Some(v) = t.game_mode {
            self.set_game_mode(v);
        }
        if let Some(v) = t.visit_flags {
            self.set_visit_flags(v);
        }
        if let Some(v) = t.save_number {
            self.set_save_number(v);
        }
        if let Some(v) = t.save_point {
            self.set_save_point(v);
        }
        if let Some(v) = t.companion_type {
            self.set_companion_type(v);
        }
        if let Some(v) = t.companion_variant {
            self.set_companion_variant(v);
        }
        if let Some(v) = t.companion_hp {
            self.set_companion_hp(v);
        }
        if let Some(v) = t.companion_hp_max {
            self.set_companion_hp_max(v);
        }
        if let Some(v) = t.player_exp {
            self.set_player_exp(v);
        }
        if let Some(v) = t.player_bp {
            self.set_player_bp(v);
        }
        if let Some(v) = t.max_exp {
            self.set_max_exp(v);
        }
        if let Some(v) = t.max_bp {
            self.set_max_bp(v);
        }
        if let Some(v) = t.hp_bonus {
            self.set_hp_bonus(v);
        }
        if let Some(v) = t.mp_bonus {
            self.set_mp_bonus(v);
        }
        if let Some(v) = t.character_id {
            self.set_character_id(v);
        }
        if let Some(v) = t.battles_won {
            self.set_battles_won(v);
        }
        if let Some(v) = t.battles_escaped {
            self.set_battles_escaped(v);
        }
        if let Some(v) = t.save_count {
            self.set_save_count(v);
        }
        if let Some(v) = t.replay_rank {
            self.set_replay_rank(v);
        }
        if let Some(v) = t.shop_tiers {
            self.set_shop_tiers(v);
        }
        if let Some(v) = t.shop_stock {
            self.set_shop_stock(v);
        }
        if let Some(v) = t.vibration_off {
            self.set_vibration_off(v);
        }
        if let Some(v) = t.run_default {
            self.set_run_default(v);
        }
        if let Some(v) = t.button_layout {
            self.set_button_layout(v);
        }
        if let Some(v) = t.mono_sound {
            self.set_mono_sound(v);
        }
        if let Some(v) = t.music_volume {
            self.set_music_volume(v);
        }
        if let Some(v) = t.cursor_reset {
            self.set_cursor_reset(v);
        }
        if let Some(v) = t.interlaced {
            self.set_interlaced(v);
        }
        if let Some(v) = t.cheat_mode {
            self.set_cheat_mode(v);
        }
        if let Some(v) = t.actors_frozen {
            self.set_actors_frozen(v);
        }
        if let Some(rows) = &t.item_rows {
            for (i, row) in rows.iter().enumerate().take(ITEM_ROW_COUNT) {
                match row {
                    Some(row) => self.set_item_row(i, *row),
                    None => self.set_item_row(
                        i,
                        ItemRow {
                            item_id: 0,
                            attach_slot: 0,
                            qty: 0,
                        },
                    ),
                }
            }
        }
        if let Some(r) = t.carried_range {
            self.set_carried_range(r);
        }
        if let Some(loads) = &t.weapon_loads {
            for (i, load) in loads.iter().enumerate().take(WEAPON_LOAD_COUNT) {
                if let Some(load) = load {
                    self.set_weapon_load(i, *load);
                }
            }
        }
        if let Some(counts) = &t.weapon_use_counts {
            for (i, v) in counts.iter().enumerate().take(WEAPON_USE_COUNT) {
                self.set_weapon_use_count(i, *v);
            }
        }
        if let Some(levels) = &t.pe_levels {
            for (i, v) in levels.iter().enumerate().take(PE_LEVEL_COUNT) {
                self.set_pe_level(i, *v);
            }
        }
        if let Some(counts) = &t.pe_use_counts {
            for (i, v) in counts.iter().enumerate().take(PE_USE_COUNT) {
                self.set_pe_use_count(i, *v);
            }
        }
        if let Some(bonus) = &t.item_level_bonus {
            for (i, v) in bonus.iter().enumerate().take(ITEM_LEVEL_BONUS_COUNT) {
                self.set_item_level_bonus(i, *v);
            }
        }
        if let Some(bits) = &t.collected_bits {
            for &b in bits {
                if b < COLLECTED_BIT_WORDS * 32 {
                    self.set_collected_bit(b, true);
                }
            }
        }
        if let Some(bits) = &t.item_seen_bits {
            for &b in bits {
                if b < ITEM_SEEN_BIT_WORDS * 32 {
                    self.set_item_seen_bit(b, true);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn item_rows_roundtrip() {
        let mut save = SaveImage::blank();
        save.set_item_row(
            3,
            ItemRow {
                item_id: 0x85,
                attach_slot: -1,
                qty: 7,
            },
        );
        assert_eq!(
            save.item_row(3),
            ItemRow {
                item_id: 0x85,
                attach_slot: -1,
                qty: 7
            }
        );
        save.set_item_row(300.min(ITEM_ROW_COUNT - 1), ItemRow::default());
    }

    #[test]
    fn transfer_roundtrip_preserves_unmodeled() {
        let mut save = SaveImage::blank();
        // Enemy-pose bytes are unmodeled: a transfer must neither read nor write them.
        save.raw[0x30] = 0xAB;
        save.set_player_exp(1234);
        let transfer = save.to_transfer();
        let json = serde_json::to_string(&transfer).unwrap();
        let mut other = SaveImage::blank();
        other.apply_transfer(&serde_json::from_str(&json).unwrap());
        assert_eq!(other.player_exp(), 1234);
        assert_eq!(other.raw[0x30], 0); // unmodeled bytes are untouched by import
    }

    #[test]
    fn clamps() {
        let mut save = SaveImage::blank();
        save.set_play_time_minutes(60000);
        assert_eq!(save.play_time_minutes(), 59999);
        save.set_battles_won(20000);
        assert_eq!(save.battles_won(), 9999);
        save.set_pe_level(0, 9);
        assert_eq!(save.pe_level(0), 3);
    }
}
