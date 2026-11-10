fn main() {
    let mut data = vec![0u8; pe2_save_editor::layout::FILE_SIZE];
    data[0] = b'S';
    data[1] = b'C';
    let mut slot = pe2_save_editor::edit::SlotData::from_bytes(data).unwrap();
    slot.edit(|save| save.set_player_exp(4242)).unwrap();
    // recompute what write_section_checksum did on the save section
    let s = pe2_save_editor::layout::SAVE_SECTION;
    let rec = &slot.data[s.live_offset..s.live_offset + s.record_bytes];
    let sum = pe2_save_editor::checksum::signed_byte_sum(&rec[4..]);
    println!("stored {:?} computed {}", &rec[0..4], sum);
}
