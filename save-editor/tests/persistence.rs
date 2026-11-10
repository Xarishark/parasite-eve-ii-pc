use pe2_save_editor::{
    edit::SlotData,
    layout::{FILE_SIZE, PLAYER_SECTION},
};
use std::{fs, process::Command};

#[test]
fn cli_edit_persists_player_currency_in_standalone_file() {
    let root = std::env::temp_dir().join(format!("pe2-cli-persist-{}", std::process::id()));
    fs::create_dir_all(&root).unwrap();
    let path = root.join("copy.slot");
    let mut data = vec![0; FILE_SIZE];
    data[..2].copy_from_slice(b"SC");
    fs::write(&path, data).unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_pe2-save-editor"))
        .arg("edit")
        .arg(&path)
        .args(["--set", "bp=4321", "--set", "exp=777"])
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let slot = SlotData::from_bytes(fs::read(&path).unwrap()).unwrap();
    assert_eq!(slot.save().unwrap().player_bp(), 4321);
    let offset = PLAYER_SECTION.live_offset;
    assert_eq!(&slot.data[offset + 8..offset + 12], &777i32.to_le_bytes());
    assert_eq!(&slot.data[offset + 12..offset + 16], &4321i32.to_le_bytes());
    let (sections, header, summary) = slot.verify();
    assert!(sections.iter().all(|ok| *ok) && header && summary);
    fs::remove_dir_all(root).unwrap();
}
