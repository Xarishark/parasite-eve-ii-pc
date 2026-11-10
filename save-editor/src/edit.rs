//! Save editing operations and full-file checksum maintenance.
//!
//! Mutating operations act on [`SaveImage`]; `refresh_checksums` then updates
//! the whole slot file the way the game's save walk does: section checksums
//! (live copies only), the summary pair, the header checksum and the card
//! file header sum. Loading in-game only reads live copies, so backups are
//! deliberately left untouched.

use anyhow::{bail, Result};

use crate::checksum::{
    verify_save_header_checksum, verify_section_checksum, write_all_section_checksums,
    write_card_header_checksum, write_save_header_checksum, write_section_checksum_summary,
};
use crate::layout::{CHECKSUM_SECTIONS, PLAYER_SECTION, SAVE_SECTION};
use crate::save::{SaveImage, SAVE_IMAGE_SIZE};

/// A whole slot file (0x2000 bytes): card header + all sections.
#[derive(Debug, Clone)]
pub struct SlotData {
    pub data: Vec<u8>,
}

impl SlotData {
    pub fn from_bytes(data: Vec<u8>) -> Result<Self> {
        if data.len() != crate::layout::FILE_SIZE {
            bail!(
                "slot file must be {} bytes, got {}",
                crate::layout::FILE_SIZE,
                data.len()
            );
        }
        Ok(Self { data })
    }

    /// Typed view of the live save image.
    pub fn save(&self) -> Result<SaveImage> {
        SaveImage::from_bytes(
            self.data[SAVE_SECTION.live_offset..SAVE_SECTION.live_offset + SAVE_IMAGE_SIZE]
                .to_vec(),
        )
    }

    /// Mutable typed view source: the live image bytes.
    fn image_mut(&mut self) -> &mut [u8] {
        &mut self.data[SAVE_SECTION.live_offset..SAVE_SECTION.live_offset + SAVE_IMAGE_SIZE]
    }

    /// Run an edit closure against the live save image, then refresh checksums.
    pub fn edit(&mut self, f: impl FnOnce(&mut SaveImage)) -> Result<()> {
        let mut image = self.save()?;
        let old_exp = image.player_exp();
        let old_bp = image.player_bp();
        f(&mut image);
        // McSaveData carries the menu preview. Gameplay restores EXP and BP
        // from the separate PlayerStatus record, so edits must update both.
        // Only propagate changed fields: unrelated edits must preserve the
        // live player record even when the preview was already stale.
        for (old, new, offset) in [
            (old_exp, image.player_exp(), 0x08),
            (old_bp, image.player_bp(), 0x0c),
        ] {
            if old != new {
                let start = PLAYER_SECTION.live_offset + offset;
                self.data[start..start + 4].copy_from_slice(&new.to_le_bytes());
            }
        }
        let bytes = image.raw;
        self.image_mut().copy_from_slice(&bytes);
        self.refresh_checksums();
        Ok(())
    }

    /// Recompute all checksum pairs the way the game's save walk writes them.
    ///
    /// Order matters: the header, title and (zeroed) buffer checksum pairs sit
    /// inside the save section's checksummed payload, so they are written
    /// before the section checksums; the summary is written last.
    pub fn refresh_checksums(&mut self) {
        let mut image = std::mem::take(&mut self.data);
        let header: Vec<u8> = image[..SAVE_SECTION.live_offset].to_vec();
        write_card_header_checksum(&header, &mut image[SAVE_SECTION.live_offset..]);
        write_save_header_checksum(&mut image[SAVE_SECTION.live_offset..]);
        // Zero the buffer-checksum pair before section sums cover it.
        const BUFFER_CHECKSUM_OFFSET: usize = 0x940;
        let live = SAVE_SECTION.live_offset;
        image[live + BUFFER_CHECKSUM_OFFSET..live + BUFFER_CHECKSUM_OFFSET + 2]
            .copy_from_slice(&0u16.to_le_bytes());
        image[live + BUFFER_CHECKSUM_OFFSET + 2..live + BUFFER_CHECKSUM_OFFSET + 4]
            .copy_from_slice(&crate::checksum::CHECKSUM_MASK.to_le_bytes());
        write_all_section_checksums(&mut image);
        write_section_checksum_summary(&mut image);
        self.data = image;
    }

    /// Verify every live section checksum and the summary pair.
    ///
    /// Returns `(section_results, header_ok, summary_ok)` in section order.
    pub fn verify(&self) -> (Vec<bool>, bool, bool) {
        let mut sections = Vec::new();
        for section in CHECKSUM_SECTIONS {
            let record =
                &self.data[section.live_offset..section.live_offset + section.record_bytes];
            sections.push(verify_section_checksum(record));
        }
        let image =
            &self.data[SAVE_SECTION.live_offset..SAVE_SECTION.live_offset + SAVE_IMAGE_SIZE];
        let header_ok = verify_save_header_checksum(image);
        let mut sum: u16 = 0;
        for section in CHECKSUM_SECTIONS {
            sum = sum.wrapping_add(self.data[section.live_offset] as u16);
        }
        let stored = u16::from_le_bytes(
            self.data[SAVE_SECTION.live_offset + 0x940..SAVE_SECTION.live_offset + 0x942]
                .try_into()
                .unwrap(),
        );
        let summary_ok = stored == sum;
        (sections, header_ok, summary_ok)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::save::ItemRow;

    /// Slot file shaped like the game writes it: 'SC' header section, zeroed
    /// sections. Checksums start invalid until refreshed.
    fn synthetic_slot() -> SlotData {
        let mut data = vec![0u8; crate::layout::FILE_SIZE];
        data[0] = b'S';
        data[1] = b'C';
        SlotData::from_bytes(data).unwrap()
    }

    #[test]
    fn edit_updates_checksums_and_verify_passes() {
        let mut slot = synthetic_slot();
        slot.edit(|save| {
            save.set_player_exp(4242);
            save.set_item_row(
                0,
                ItemRow {
                    item_id: 0x85,
                    attach_slot: 0,
                    qty: 3,
                },
            );
        })
        .unwrap();
        let (sections, header_ok, summary_ok) = slot.verify();
        assert!(
            sections.iter().all(|ok| *ok),
            "all live section checksums must verify"
        );
        assert!(header_ok);
        assert!(summary_ok);
        let save = slot.save().unwrap();
        assert_eq!(save.player_exp(), 4242);
        assert_eq!(save.item_row(0).item_id, 0x85);
    }

    #[test]
    fn unmodeled_bytes_survive_edit() {
        let mut slot = synthetic_slot();
        // Plant a byte in a region the editor does not model (enemy poses).
        slot.data[SAVE_SECTION.live_offset + 0x30] = 0x5A;
        slot.edit(|save| save.set_player_bp(99)).unwrap();
        assert_eq!(slot.data[SAVE_SECTION.live_offset + 0x30], 0x5A);
        assert_eq!(slot.save().unwrap().player_bp(), 99);
    }

    #[test]
    fn verify_detects_corruption() {
        let mut slot = synthetic_slot();
        slot.edit(|save| save.set_player_exp(1)).unwrap();
        // Corrupt a payload byte.
        slot.data[SAVE_SECTION.live_offset + 0x14] ^= 0xFF;
        let (sections, _, _) = slot.verify();
        assert!(
            !sections[0],
            "save section must fail verification after corruption"
        );
    }
    #[test]
    fn currency_edit_updates_gameplay_record_and_preserves_unrelated_bytes() {
        let mut slot = synthetic_slot();
        let base = PLAYER_SECTION.live_offset;
        slot.data[base + 0x10] = 0x5a;
        slot.data[PLAYER_SECTION.backup_offset() + 0x0c] = 0x42;
        slot.edit(|s| {
            s.set_player_exp(777);
            s.set_player_bp(4321);
        })
        .unwrap();
        assert_eq!(&slot.data[base + 8..base + 12], &777i32.to_le_bytes());
        assert_eq!(&slot.data[base + 12..base + 16], &4321i32.to_le_bytes());
        assert_eq!(slot.data[base + 0x10], 0x5a);
        assert_eq!(slot.data[PLAYER_SECTION.backup_offset() + 0x0c], 0x42);
        // A stale preview must not overwrite actual gameplay BP on another edit.
        slot.data[base + 12..base + 16].copy_from_slice(&987i32.to_le_bytes());
        slot.edit(|s| s.set_player_exp(778)).unwrap();
        assert_eq!(&slot.data[base + 12..base + 16], &987i32.to_le_bytes());
        assert!(slot.verify().0.iter().all(|ok| *ok));
    }
}
