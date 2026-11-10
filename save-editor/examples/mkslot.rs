// Writes one syntactically valid PE2 slot with refreshed checksums.
fn main() {
    let path = std::env::args().nth(1).unwrap();
    let mut data = vec![0u8; pe2_save_editor::layout::FILE_SIZE];
    data[0] = b'S';
    data[1] = b'C';
    let mut slot = pe2_save_editor::edit::SlotData::from_bytes(data).unwrap();
    slot.edit(|save| {
        save.set_player_exp(777);
        save.set_save_point(6); // Motel Lobby
    })
    .unwrap();
    std::fs::write(path, slot.data).unwrap();
}
