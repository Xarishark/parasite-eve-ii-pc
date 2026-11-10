//! Memory-card containers: PsyZ directories, raw slot files, `.mcs` single
//! saves and full card images (`.srm` / `.mcr` / `.mcd` / `.gme`).
//!
//! All formats share the PS1 card conventions (128-byte sectors, 8 KB files,
//! 15 user slots). Non-PE2 files and container headers are preserved
//! byte-for-byte; only whole PE2 slot files are read or written.

use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};

use crate::layout::{FILE_SIZE, MAX_CARD_SLOTS, PRODUCT_CODE, SECTOR_SIZE};

/// 128 KB raw PS1 card image.
pub const PS1_CARD_SIZE: usize = 0x20000;
/// DexDrive `.gme` header preceding the card image.
pub const GME_HEADER_SIZE: usize = 0x0F40;
/// `.mcs` single save: 128-byte directory header + one 8 KB file.
pub const MCS_SAVE_SIZE: usize = FILE_SIZE + 0x80;

/// One PE2 save file with the card filename it came from.
#[derive(Debug, Clone)]
pub struct SlotFile {
    /// Card filename, e.g. `BASLUS-01042A3xK9mQz` (20 chars): the product code, the slot letter, then seven
    /// random characters the game picks when it creates the file.
    pub filename: String,
    /// The 0x2000-byte slot file contents.
    pub data: Vec<u8>,
}

/// Characters the game draws the filename suffix from (`_gMcFileNameSuffixAlphabet`).
const SUFFIX_ALPHABET: &str = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789;:";

/// Which slot a filename occupies (`'A'..='O'`), if any.
///
/// The game names a save `BASLUS-01042` + one slot character (its directory index in the suffix alphabet) + seven
/// random characters, 20 in all.
pub fn slot_letter(filename: &str) -> Option<usize> {
    let rest = filename.strip_prefix(PRODUCT_CODE)?;
    let letter = rest.chars().next()?;
    let index = SUFFIX_ALPHABET.find(letter)?;
    (index < MAX_CARD_SLOTS).then_some(index)
}

/// Build a card filename for a new slot (`0 -> BASLUS-01042A0000000`, `14 -> ...O0000000`).
///
/// An existing slot keeps its own name: the seven trailing characters are random in the game and not meaningful.
pub fn filename_for_slot(index: usize) -> String {
    let letter = SUFFIX_ALPHABET.as_bytes()[index.min(MAX_CARD_SLOTS - 1)] as char;
    format!("{PRODUCT_CODE}{letter}0000000")
}

fn looks_like_pe2_file(data: &[u8]) -> bool {
    data.len() == FILE_SIZE && data.starts_with(b"SC")
}

fn read_pe2_file(path: &Path) -> Result<SlotFile> {
    let data = fs::read(path).with_context(|| format!("reading {}", path.display()))?;
    if !looks_like_pe2_file(&data) {
        bail!(
            "{} is not a PE2 save file (expected {} bytes starting with 'SC')",
            path.display(),
            FILE_SIZE
        );
    }
    Ok(SlotFile {
        filename: path
            .file_name()
            .unwrap_or_default()
            .to_string_lossy()
            .into_owned(),
        data,
    })
}

/// A memory card container holding up to 15 PE2 slots.
#[derive(Debug, Clone)]
pub enum CardContainer {
    /// Directory of per-slot files (`card0/`, `card1/`).
    Directory {
        path: PathBuf,
        slots: Vec<Option<SlotFile>>,
    },
    /// One raw 0x2000-byte slot file.
    RawFile(SlotFile),
    /// `.mcs`: 128-byte header + one slot file.
    Mcs { header: Vec<u8>, slot: SlotFile },
    /// Full card image, optionally with a DexDrive header.
    CardImage {
        path: PathBuf,
        gme_header: Option<Vec<u8>>,
        raw: Vec<u8>,
    },
}

impl CardContainer {
    /// Open any supported container.
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref();
        if path.is_dir() {
            return Self::open_directory(path);
        }
        let data = fs::read(path).with_context(|| format!("reading {}", path.display()))?;
        match data.len() {
            FILE_SIZE if data.starts_with(b"SC") => Ok(Self::RawFile(SlotFile {
                filename: path
                    .file_name()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .into_owned(),
                data,
            })),
            MCS_SAVE_SIZE if &data[0..2] == b"SC" && data[0x80..0x82].iter().eq(b"SC".iter()) => {
                Ok(Self::Mcs {
                    header: data[..0x80].to_vec(),
                    slot: SlotFile {
                        filename: decode_mcs_filename(&data)?,
                        data: data[0x80..].to_vec(),
                    },
                })
            }
            PS1_CARD_SIZE => Ok(Self::CardImage {
                path: path.to_path_buf(),
                gme_header: None,
                raw: data,
            }),
            n if n == PS1_CARD_SIZE + GME_HEADER_SIZE && data.starts_with(b"123-456-789") => {
                Ok(Self::CardImage {
                    path: path.to_path_buf(),
                    gme_header: Some(data[..GME_HEADER_SIZE].to_vec()),
                    raw: data[GME_HEADER_SIZE..].to_vec(),
                })
            }
            _ => bail!(
                "{}: unrecognized container size {}",
                path.display(),
                data.len()
            ),
        }
    }

    fn open_directory(path: &Path) -> Result<Self> {
        let mut slots: Vec<Option<SlotFile>> = vec![None; MAX_CARD_SLOTS];
        let mut entries: Vec<PathBuf> = fs::read_dir(path)
            .with_context(|| format!("listing {}", path.display()))?
            .filter_map(|e| e.ok().map(|e| e.path()))
            .collect();
        entries.sort();
        for entry in entries {
            if !entry.is_file() {
                continue;
            }
            let name = entry
                .file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned();
            if let Some(index) = slot_letter(&name) {
                slots[index] = Some(read_pe2_file(&entry)?);
            }
        }
        Ok(Self::Directory {
            path: path.to_path_buf(),
            slots,
        })
    }

    /// The PE2 slot files this container exposes, with their slot indices.
    pub fn slots(&self) -> Vec<(usize, SlotFile)> {
        match self {
            Self::Directory { slots, .. } => slots
                .iter()
                .enumerate()
                .filter_map(|(i, s)| s.as_ref().map(|f| (i, f.clone())))
                .collect(),
            Self::RawFile(file) => vec![(slot_letter(&file.filename).unwrap_or(0), file.clone())],
            Self::Mcs { slot, .. } => {
                vec![(slot_letter(&slot.filename).unwrap_or(0), slot.clone())]
            }
            Self::CardImage { raw, .. } => read_card_image_slots(raw),
        }
    }

    /// Replace one slot's data in the container.
    pub fn write_slot(&mut self, index: usize, data: &[u8]) -> Result<()> {
        if index >= MAX_CARD_SLOTS {
            bail!("slot index {} out of range", index);
        }
        if data.len() != FILE_SIZE {
            bail!("slot data must be {} bytes", FILE_SIZE);
        }
        match self {
            Self::Directory { path, slots } => {
                let filename = slots[index]
                    .as_ref()
                    .map(|f| f.filename.clone())
                    .unwrap_or_else(|| filename_for_slot(index));
                let file_path = path.join(&filename);
                fs::write(&file_path, data)
                    .with_context(|| format!("writing {}", file_path.display()))?;
                slots[index] = Some(SlotFile {
                    filename,
                    data: data.to_vec(),
                });
            }
            Self::RawFile(file) => {
                file.data = data.to_vec();
                // A raw file has no directory name to derive the slot from when absent.
                if slot_letter(&file.filename).is_none() {
                    file.filename = filename_for_slot(index);
                }
            }
            Self::Mcs { slot, .. } => {
                slot.data = data.to_vec();
                if slot_letter(&slot.filename).is_none() {
                    slot.filename = filename_for_slot(index);
                }
            }
            Self::CardImage { raw, .. } => {
                // Overwrite only a block the card's directory already lists for this slot.
                let block = (1..=MAX_CARD_SLOTS)
                    .find(|&b| {
                        let entry = b * SECTOR_SIZE;
                        raw[entry] == 0x51
                            && slot_letter(&String::from_utf8_lossy(
                                &raw[entry + 0x0A..entry + 0x1E],
                            )) == Some(index)
                    })
                    .ok_or_else(|| {
                        anyhow::anyhow!("slot {} has no PE2 save on this card image", index)
                    })?;
                raw[block * FILE_SIZE..(block + 1) * FILE_SIZE].copy_from_slice(data);
            }
        }
        Ok(())
    }

    /// Persist container-level changes (card images, `.mcs`, raw files).
    /// Persist a container opened from `path`, including standalone saves.
    /// The caller retains the source path because raw and MCS files store only
    /// their in-card filename, which is not necessarily their host filename.
    pub fn save_to_path(&self, path: impl AsRef<Path>) -> Result<()> {
        match self {
            Self::RawFile(file) => write_slot_file(path, &file.data),
            Self::Mcs { header, slot } => {
                let mut bytes = header.clone();
                bytes.extend_from_slice(&slot.data);
                fs::write(path.as_ref(), bytes)
                    .with_context(|| format!("writing {}", path.as_ref().display()))
            }
            _ => self.save(),
        }
    }

    pub fn save(&self) -> Result<()> {
        match self {
            Self::Directory { .. } => Ok(()), // slot writes already hit the filesystem
            Self::RawFile(file) => {
                let _ = file;
                bail!("raw files are written via write_slot_to_path")
            }
            Self::Mcs { .. } => bail!("use save_mcs to persist"),
            Self::CardImage {
                path,
                gme_header,
                raw,
            } => {
                let mut out = gme_header.clone().unwrap_or_default();
                out.extend_from_slice(raw);
                fs::write(path, out).with_context(|| format!("writing {}", path.display()))
            }
        }
    }
}

/// Decode the card filename embedded in a `.mcs` directory header.
fn decode_mcs_filename(data: &[u8]) -> Result<String> {
    let bytes = &data[0x0A..0x0A + 20];
    let name: String = bytes
        .iter()
        .map(|&b| {
            if b.is_ascii_graphic() || b == b'_' {
                b as char
            } else {
                '_'
            }
        })
        .collect();
    Ok(name)
}

/// Locate PE2 slot files inside a raw card image through the card's own directory.
///
/// Frame 0 is the card header; frames 1..=15 are 128-byte directory entries for blocks 1..=15. An entry whose first
/// byte is 0x51 starts a file in use (0x52 and 0x53 continue or end it, 0xA0 is free); the filename is 20 bytes at
/// offset 0x0A. PE2 saves are one block, so block `n` holds its data at `n * 0x2000`.
fn read_card_image_slots(raw: &[u8]) -> Vec<(usize, SlotFile)> {
    let mut found = Vec::new();
    for block in 1..=MAX_CARD_SLOTS {
        let entry = block * SECTOR_SIZE;
        if entry + SECTOR_SIZE > raw.len() || raw[entry] != 0x51 {
            continue;
        }
        let filename: String = raw[entry + 0x0A..entry + 0x0A + 20]
            .iter()
            .map(|&b| if b.is_ascii_graphic() { b as char } else { '_' })
            .collect();
        if slot_letter(&filename).is_none() {
            continue;
        }
        let content = block * FILE_SIZE;
        if content + FILE_SIZE > raw.len() || raw[content..content + 2] != *b"SC" {
            continue;
        }
        found.push((
            slot_letter(&filename).unwrap_or(block - 1),
            SlotFile {
                filename,
                data: raw[content..content + FILE_SIZE].to_vec(),
            },
        ));
    }
    found
}

/// Write a slot file to an arbitrary path (export).
pub fn write_slot_file(path: impl AsRef<Path>, data: &[u8]) -> Result<()> {
    if data.len() != FILE_SIZE {
        bail!("slot data must be {} bytes", FILE_SIZE);
    }
    fs::write(&path, data).with_context(|| format!("writing {}", path.as_ref().display()))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A syntactically valid slot file: 'SC' magic + size.
    #[allow(dead_code)]
    pub fn synthetic_slot(seed: u8) -> Vec<u8> {
        let mut data = vec![0u8; FILE_SIZE];
        data[0] = b'S';
        data[1] = b'C';
        data[0x210] = seed; // inside the save image, past the checksum header
        data
    }

    #[test]
    fn slot_letters() {
        assert_eq!(slot_letter("BASLUS-01042A3xK9mQz"), Some(0));
        assert_eq!(slot_letter("BASLUS-01042O0000000"), Some(14));
        assert_eq!(slot_letter("BASLUS-01042P0000000"), None);
        assert_eq!(slot_letter("BASLUS-01042"), None);
        assert_eq!(slot_letter("SCUS-94163"), None);
    }

    #[test]
    fn filenames_round_trip() {
        for index in 0..MAX_CARD_SLOTS {
            let name = filename_for_slot(index);
            assert_eq!(name.len(), 20);
            assert_eq!(slot_letter(&name), Some(index));
        }
    }

    #[test]
    fn card_image_directory_is_read_and_written() {
        let mut raw = vec![0u8; PS1_CARD_SIZE];
        for block in 1..=MAX_CARD_SLOTS {
            raw[block * SECTOR_SIZE] = 0xA0; // free
        }
        let entry = 3 * SECTOR_SIZE; // block 3 holds slot index 2 ('C')
        raw[entry] = 0x51;
        raw[entry + 0x0A..entry + 0x0A + 20].copy_from_slice(b"BASLUS-01042Cabcdefg");
        raw[3 * FILE_SIZE..3 * FILE_SIZE + 2].copy_from_slice(b"SC");
        let mut card = CardContainer::CardImage {
            path: PathBuf::new(),
            gme_header: None,
            raw,
        };
        let slots = card.slots();
        assert_eq!(slots.len(), 1);
        assert_eq!(slots[0].0, 2);
        assert_eq!(slots[0].1.filename, "BASLUS-01042Cabcdefg");
        let mut data = synthetic_slot(7);
        data[0x300] = 0x5A;
        card.write_slot(2, &data).unwrap();
        assert_eq!(card.slots()[0].1.data[0x300], 0x5A);
        assert!(
            card.write_slot(4, &data).is_err(),
            "no PE2 save listed for slot 4"
        );
    }

    #[test]
    fn directory_write_keeps_the_games_filename() {
        let dir = std::env::temp_dir().join(format!("pe2-card-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let name = "BASLUS-01042B9zQ4kL2";
        std::fs::write(dir.join(name), synthetic_slot(1)).unwrap();
        let mut card = CardContainer::open(&dir).unwrap();
        card.write_slot(1, &synthetic_slot(2)).unwrap();
        let files: Vec<_> = std::fs::read_dir(&dir)
            .unwrap()
            .filter_map(|e| e.ok())
            .collect();
        assert_eq!(files.len(), 1, "an edit must not create a second file");
        assert_eq!(std::fs::read(dir.join(name)).unwrap()[0x210], 2);
        let _ = std::fs::remove_dir_all(&dir);
    }
}
