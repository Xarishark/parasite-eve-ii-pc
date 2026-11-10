//! Checksum algorithms of the PE2 memory card save file.
//!
//! Every sum retains its low 16 bits and is stored as `u16` with a stored
//! ones' complement (`0xFFFF - sum`). Payload bytes are summed **as signed
//! bytes** (`(s8)byte`), except the section summary which sums the unsigned
//! low byte of each section checksum.

use crate::layout::CHECKSUM_SECTIONS;

/// Ones' complement mask (`MEMORY_CARD_CHECKSUM_MASK`).
pub const CHECKSUM_MASK: u16 = 0xFFFF;
/// Signed-byte sum span of the save header (`MEMORY_CARD_SAVE_HEADER_CHECKSUM_BYTES`).
pub const HEADER_CHECKSUM_BYTES: usize = 0x38;
/// Offset of `location` within the save image; the header checksum starts here.
pub const SAVE_LOCATION_OFFSET: usize = 0x04;
/// Offset of `headerChecksum` within the save image.
pub const SAVE_HEADER_CHECKSUM_OFFSET: usize = 0x1C;

/// Signed-byte sum of `bytes`, retaining the low 16 bits.
pub fn signed_byte_sum(bytes: &[u8]) -> u16 {
    let mut sum: i16 = 0;
    for &b in bytes {
        sum = sum.wrapping_add(b as i8 as i16);
    }
    sum as u16
}

/// Write a checksum pair (`checksum`, `checksumComplement`) for `payload`.
///
/// `dst` points at the pair inside the record; `payload` is the record body
/// after the 4-byte checksum header. Mirrors `_mcWriteSaveSectionChecksums`.
pub fn write_section_checksum(record: &mut [u8]) {
    assert!(
        record.len() >= 4,
        "checksummed record needs a 4-byte header"
    );
    let sum = signed_byte_sum(&record[4..]);
    record[0..2].copy_from_slice(&sum.to_le_bytes());
    record[2..4].copy_from_slice(&(CHECKSUM_MASK - sum).to_le_bytes());
}

/// Return whether the record's stored checksum pair matches its payload.
pub fn verify_section_checksum(record: &[u8]) -> bool {
    if record.len() < 4 {
        return false;
    }
    let stored = u16::from_le_bytes([record[0], record[1]]);
    let stored_comp = u16::from_le_bytes([record[2], record[3]]);
    let sum = signed_byte_sum(&record[4..]);
    stored == sum && stored_comp == CHECKSUM_MASK - sum
}

/// Recompute the save header checksum pair (56 signed bytes from `location`).
///
/// Mirrors `_mcWriteSaveHeaderChecksum`: the pair itself is zeroed and set to
/// `0xFFFF` before summing so its signed-byte contribution stays -2.
pub fn write_save_header_checksum(save: &mut [u8]) {
    const CHECKSUM: usize = SAVE_HEADER_CHECKSUM_OFFSET;
    const COMPLEMENT: usize = SAVE_HEADER_CHECKSUM_OFFSET + 2;
    save[CHECKSUM..CHECKSUM + 2].copy_from_slice(&0u16.to_le_bytes());
    save[COMPLEMENT..COMPLEMENT + 2].copy_from_slice(&CHECKSUM_MASK.to_le_bytes());
    let end = SAVE_LOCATION_OFFSET + HEADER_CHECKSUM_BYTES;
    let sum = signed_byte_sum(&save[SAVE_LOCATION_OFFSET..end]);
    save[CHECKSUM..CHECKSUM + 2].copy_from_slice(&sum.to_le_bytes());
    save[COMPLEMENT..COMPLEMENT + 2].copy_from_slice(&(CHECKSUM_MASK - sum).to_le_bytes());
}

/// Verify the save header checksum pair without rewriting it.
///
/// The stored complement contributes its own signed bytes; the check mirrors
/// `_mcVerifySaveHeaderChecksum` by recomputing the sum with the stored pair
/// in place and comparing against the stored values.
pub fn verify_save_header_checksum(save: &[u8]) -> bool {
    let start = SAVE_LOCATION_OFFSET;
    let end = SAVE_LOCATION_OFFSET + HEADER_CHECKSUM_BYTES;
    let sum = signed_byte_sum(&save[start..end]);
    let stored = u16::from_le_bytes([
        save[SAVE_HEADER_CHECKSUM_OFFSET],
        save[SAVE_HEADER_CHECKSUM_OFFSET + 1],
    ]);
    let stored_comp = u16::from_le_bytes([
        save[SAVE_HEADER_CHECKSUM_OFFSET + 2],
        save[SAVE_HEADER_CHECKSUM_OFFSET + 3],
    ]);
    stored == sum && stored_comp == CHECKSUM_MASK - sum
}

/// Recompute the card file header's signed-byte sum into the save image's
/// `titleChecksum` pair. Mirrors `_mcWriteCardHeaderChecksum`.
pub fn write_card_header_checksum(file: &[u8], save: &mut [u8]) {
    const TITLE_CHECKSUM_OFFSET: usize = 0x93C;
    let sum = signed_byte_sum(&file[..0x200]);
    save[TITLE_CHECKSUM_OFFSET..TITLE_CHECKSUM_OFFSET + 2].copy_from_slice(&sum.to_le_bytes());
    save[TITLE_CHECKSUM_OFFSET + 2..TITLE_CHECKSUM_OFFSET + 4]
        .copy_from_slice(&(CHECKSUM_MASK - sum).to_le_bytes());
}

/// Recompute every live section checksum (sections 1..8).
pub fn write_all_section_checksums(file: &mut [u8]) {
    for section in CHECKSUM_SECTIONS {
        let live = &mut file[section.live_offset..section.live_offset + section.record_bytes];
        write_section_checksum(live);
    }
}

/// Recompute the `bufferChecksum` pair from the live sections' low checksum
/// bytes. Mirrors `_mcWriteSectionChecksumSummary` (`~sum` complement).
pub fn write_section_checksum_summary(file: &mut [u8]) {
    const BUFFER_CHECKSUM_OFFSET: usize = 0x940;
    let mut sum: u16 = 0;
    for section in CHECKSUM_SECTIONS {
        let live = &file[section.live_offset..section.live_offset + section.record_bytes];
        sum = sum.wrapping_add(live[0] as u16);
    }
    let save = &mut file[crate::layout::SAVE_SECTION.live_offset..];
    save[BUFFER_CHECKSUM_OFFSET..BUFFER_CHECKSUM_OFFSET + 2].copy_from_slice(&sum.to_le_bytes());
    save[BUFFER_CHECKSUM_OFFSET + 2..BUFFER_CHECKSUM_OFFSET + 4]
        .copy_from_slice(&(!sum).to_le_bytes());
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signed_sum_wraps() {
        // -1 (0xFF) repeated 0x10000 times sums to zero mod 2^16.
        assert_eq!(signed_byte_sum(&[0xFF; 0x10000]), 0);
        assert_eq!(signed_byte_sum(&[0x00]), 0);
        assert_eq!(signed_byte_sum(&[0x01, 0xFF]), 0);
        assert_eq!(signed_byte_sum(&[0x80]), 0xFF80); // (s8)0x80 = -128
    }

    #[test]
    fn section_checksum_roundtrip() {
        let mut record = vec![0u8; 0x10];
        record[4..].copy_from_slice(&[1, 2, 3, 0xFF, 0x80, 7, 8, 9, 10, 11, 12, 0xFF]);
        write_section_checksum(&mut record);
        assert!(verify_section_checksum(&record));
        record[7] ^= 0x01;
        assert!(!verify_section_checksum(&record));
    }
}
