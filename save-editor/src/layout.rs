//! Card-file section layout for Parasite Eve II memory card saves.
//!
//! Mirrors `_McSaveSection _gMcSaveSections[9]` from `src/main/mc.c`: nine
//! sections written back to back, each starting on a 128-byte card sector.
//! Section 0 is the card file header (`_McCardFileHeader`, 0x200 bytes, no
//! checksum). Sections 1..8 each hold a checksummed record followed by its
//! backup copy; `cardSectors` covers both copies rounded up to whole sectors.

/// Card file total size: one 8 KB PS1 card block (`blockCount == 1`).
pub const FILE_SIZE: usize = 0x2000;
/// Card sector size in bytes.
pub const SECTOR_SIZE: usize = 0x80;
/// Product code shared by every PE2 save file name (`BASLUS-01042<A..O>`).
pub const PRODUCT_CODE: &str = "BASLUS-01042";
/// Directory capacity of one PS1 memory card (15 user slots, `MEMORY_CARD_DIRECTORY_CAPACITY`).
pub const MAX_CARD_SLOTS: usize = 15;

/// One save section's extent on the card.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SectionLayout {
    /// Byte offset of the live copy from the file start.
    pub live_offset: usize,
    /// Length of one checksummed copy: 4-byte checksum header + payload.
    pub record_bytes: usize,
    /// Sectors (of `SECTOR_SIZE`) covering both copies; the next section follows.
    pub card_sectors: usize,
}

impl SectionLayout {
    /// Byte offset of the backup copy (immediately after the live copy).
    pub const fn backup_offset(&self) -> usize {
        self.live_offset + self.record_bytes
    }
    /// Payload byte count checksummed by the record header.
    pub const fn payload_bytes(&self) -> usize {
        self.record_bytes - 4
    }
    /// Total on-card extent: `card_sectors * SECTOR_SIZE`.
    pub const fn extent(&self) -> usize {
        self.card_sectors * SECTOR_SIZE
    }
}

/// Card file header section (`_McCardFileHeader`): 'S','C' magic, Shift-JIS
/// title, CLUT and three icon frames. Written without a checksum.
pub const HEADER_SECTION: SectionLayout = SectionLayout {
    live_offset: 0x0000,
    record_bytes: 0x200,
    card_sectors: 4,
};
/// Save image section (`McSaveData`, live and backup copies).
pub const SAVE_SECTION: SectionLayout = SectionLayout {
    live_offset: 0x0200,
    record_bytes: 0x944,
    card_sectors: 0x26,
};
/// Player-status section (`PlayerStatus::save` and `saveBackup`, 0x40 each).
pub const PLAYER_SECTION: SectionLayout = SectionLayout {
    live_offset: 0x1500,
    record_bytes: 0x40,
    card_sectors: 1,
};
/// Akropolis game-flag bank section.
pub const AKROPOLIS_SECTION: SectionLayout = SectionLayout {
    live_offset: 0x1580,
    record_bytes: 0x6C,
    card_sectors: 2,
};
/// Dryfield game-flag bank section.
pub const DRYFIELD_SECTION: SectionLayout = SectionLayout {
    live_offset: 0x1680,
    record_bytes: 0xB0,
    card_sectors: 3,
};
/// Dryfield-at-night game-flag bank section.
pub const DRYFIELD_NIGHT_SECTION: SectionLayout = SectionLayout {
    live_offset: 0x1800,
    record_bytes: 0x24,
    card_sectors: 1,
};
/// Mine/Shelter game-flag bank section.
pub const MINE_SHELTER_SECTION: SectionLayout = SectionLayout {
    live_offset: 0x1880,
    record_bytes: 0xE4,
    card_sectors: 4,
};
/// Neo Ark game-flag bank section.
pub const NEO_ARK_SECTION: SectionLayout = SectionLayout {
    live_offset: 0x1A80,
    record_bytes: 0xA4,
    card_sectors: 3,
};
/// Nibble flag bank section (`GameFlagNibbleBank`).
pub const NIBBLE_SECTION: SectionLayout = SectionLayout {
    live_offset: 0x1C00,
    record_bytes: 0x100,
    card_sectors: 4,
};

/// Checksummed sections 1..8 in on-card order (`_McSaveSections` minus the header).
pub const CHECKSUM_SECTIONS: [SectionLayout; 8] = [
    SAVE_SECTION,
    PLAYER_SECTION,
    AKROPOLIS_SECTION,
    DRYFIELD_SECTION,
    DRYFIELD_NIGHT_SECTION,
    MINE_SHELTER_SECTION,
    NEO_ARK_SECTION,
    NIBBLE_SECTION,
];

/// Card byte offset the game's file-list preview reads
/// (`MEMORY_CARD_SAVE_PREVIEW_FILE_OFFSET`): the live save image's start.
pub const SAVE_PREVIEW_OFFSET: usize = SAVE_SECTION.live_offset;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sections_chain_without_overlap() {
        let mut cursor = HEADER_SECTION.live_offset + HEADER_SECTION.extent();
        for section in CHECKSUM_SECTIONS {
            assert_eq!(
                section.live_offset, cursor,
                "section must follow the previous extent"
            );
            cursor += section.extent();
        }
        assert!(cursor <= FILE_SIZE, "sections must fit one 8 KB card block");
    }

    #[test]
    fn records_fit_extent() {
        for section in CHECKSUM_SECTIONS {
            assert!(section.record_bytes * 2 <= section.extent());
        }
    }
}
