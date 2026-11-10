//! egui GUI: slot browser on the left, tabbed editor on the right.

use anyhow::Result;
use eframe::egui;

use crate::card::CardContainer;
use crate::edit::SlotData;
use crate::save::{
    ItemRow, SaveImage, WeaponLoad, COLLECTED_BIT_WORDS, ITEM_ROW_COUNT, PE_LEVEL_COUNT,
    PE_USE_COUNT, WEAPON_LOAD_COUNT,
};

/// Application state.
pub struct EditorApp {
    container: Option<CardContainer>,
    container_path: String,
    status: String,
    active_slot: Option<usize>,
    /// Working copy of the active slot; edits land here until "Save slot to card".
    working: Option<SlotData>,
    tab: Tab,
    item_filter: String,
}

#[derive(PartialEq, Clone, Copy)]
enum Tab {
    Summary,
    Character,
    Inventory,
    Weapons,
    ParasiteEnergy,
    Progression,
    Options,
}

impl EditorApp {
    pub fn new() -> Self {
        Self {
            container: None,
            container_path: String::new(),
            status: "Open a card container (directory, raw slot, .mcs, .srm/.mcr/.mcd/.gme)".into(),
            active_slot: None,
            working: None,
            tab: Tab::Summary,
            item_filter: String::new(),
        }
    }

    fn load_container(&mut self, path: &str) {
        match CardContainer::open(path) {
            Ok(container) => {
                let count = container.slots().len();
                self.container = Some(container);
                self.container_path = path.to_string();
                self.active_slot = None;
                self.working = None;
                self.status = format!(
                    "Loaded {path} ({} PE2 slot{})",
                    count,
                    if count == 1 { "" } else { "s" }
                );
            }
            Err(e) => self.status = format!("Error: {e}"),
        }
    }

    fn select_slot(&mut self, index: usize) {
        let Some(container) = &self.container else {
            return;
        };
        match container.slots().into_iter().find(|(i, _)| *i == index) {
            Some((_, file)) => match SlotData::from_bytes(file.data.clone()) {
                Ok(data) => {
                    self.active_slot = Some(index);
                    self.working = Some(data);
                    self.status = format!("Editing slot {index}");
                }
                Err(e) => self.status = format!("Error: {e}"),
            },
            None => self.status = format!("Slot {index} not found"),
        }
    }

    fn commit(&mut self) {
        let Some(container) = self.container.as_mut() else {
            return;
        };
        let Some(index) = self.active_slot else {
            return;
        };
        let Some(data) = self.working.as_ref() else {
            return;
        };
        match container
            .write_slot(index, &data.data)
            .and_then(|()| container.save_to_path(&self.container_path))
        {
            Ok(()) => self.status = format!("Saved slot {index} to {}", self.container_path),
            Err(e) => self.status = format!("Error: {e}"),
        }
    }
}

impl eframe::App for EditorApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.top_panel(ctx);
        self.side_panel(ctx);
        self.central_panel(ctx);
    }
}

impl Default for EditorApp {
    fn default() -> Self {
        Self::new()
    }
}

impl EditorApp {
    fn top_panel(&mut self, ctx: &egui::Context) {
        egui::TopBottomPanel::top("top").show(ctx, |ui| {
            ui.horizontal(|ui| {
                if ui.button("Open card…").clicked() {
                    if let Some(path) = rfd::FileDialog::new()
                        .add_filter("Card containers", &["srm", "mcr", "mcd", "gme", "mcs"])
                        .add_filter("All files", &["*"])
                        .pick_file()
                    {
                        self.load_container(&path.to_string_lossy());
                    }
                }
                ui.separator();
                if ui.button("Open directory…").clicked() {
                    if let Some(path) = rfd::FileDialog::new().pick_folder() {
                        self.load_container(&path.to_string_lossy());
                    }
                }
                if let Some(index) = self.active_slot {
                    ui.separator();
                    if ui.button("Save slot to card").clicked() {
                        self.commit();
                    }
                    ui.label(format!("slot {index}"));
                }
            });
        });
    }

    fn side_panel(&mut self, ctx: &egui::Context) {
        egui::SidePanel::left("slots").show(ctx, |ui| {
            ui.heading("Slots");
            ui.separator();
            let Some(container) = &self.container else {
                ui.label(&self.status);
                return;
            };
            let slots: Vec<(usize, String, String)> = container
                .slots()
                .into_iter()
                .map(|(i, f)| {
                    let summary = SlotData::from_bytes(f.data.clone())
                        .and_then(|d| d.save())
                        .map(|s| crate::cli::describe(&s))
                        .unwrap_or_else(|e| format!("unreadable: {e}"));
                    (i, f.filename.clone(), summary)
                })
                .collect();
            for (index, filename, summary) in slots {
                let selected = self.active_slot == Some(index);
                if ui
                    .selectable_label(selected, format!("[{index}] {filename}"))
                    .clicked()
                {
                    self.select_slot(index);
                }
                ui.label(egui::RichText::new(format!("    {summary}")).small().weak());
            }
        });
    }

    fn central_panel(&mut self, ctx: &egui::Context) {
        egui::CentralPanel::default().show(ctx, |ui| {
            if self.working.is_none() {
                ui.label("Select a slot to edit.");
                return;
            }
            let tab = self.tab;
            let mut item_filter = self.item_filter.clone();
            let data = self.working.as_mut().unwrap();
            let Ok(mut save) = data.save() else {
                ui.label("Save image unreadable.");
                return;
            };
            ui.horizontal(|ui| {
                for (page, label) in [
                    (Tab::Summary, "Summary"),
                    (Tab::Character, "Character"),
                    (Tab::Inventory, "Inventory"),
                    (Tab::Weapons, "Weapons"),
                    (Tab::ParasiteEnergy, "Parasite Energy"),
                    (Tab::Progression, "Progression"),
                    (Tab::Options, "Options"),
                ] {
                    ui.selectable_value(&mut self.tab, page, label);
                }
            });
            ui.separator();
            let mut changed = false;
            egui::ScrollArea::vertical().show(ui, |ui| {
                changed |= match tab {
                    Tab::Summary => show_summary(ui, &save),
                    Tab::Character => show_character(ui, &mut save),
                    Tab::Inventory => show_inventory(ui, &mut save, &mut item_filter),
                    Tab::Weapons => show_weapons(ui, &mut save),
                    Tab::ParasiteEnergy => show_pe(ui, &mut save),
                    Tab::Progression => show_progression(ui, &mut save),
                    Tab::Options => show_options(ui, &mut save),
                };
            });
            self.item_filter = item_filter;
            if changed {
                let bytes = save.raw.clone();
                let live = crate::layout::SAVE_SECTION.live_offset;
                data.data[live..live + bytes.len()].copy_from_slice(&bytes);
                data.refresh_checksums();
            }
            ui.separator();
            ui.label(&self.status);
        });
    }
}

fn show_summary(ui: &mut egui::Ui, save: &SaveImage) -> bool {
    ui.heading("Save summary");
    ui.label(format!("Location key: {:02x?}", save.location()));
    ui.label(format!(
        "Save point: {} (#{}), label #{}",
        save.save_point_label(),
        save.save_point(),
        save.save_number()
    ));
    let minutes = save.play_time_minutes();
    ui.label(format!(
        "Play time: {} minutes (~{}h{}m)",
        minutes,
        minutes / 60,
        minutes % 60
    ));
    ui.label(format!(
        "Game mode: {}",
        crate::save::GAME_MODE_LABELS[save.game_mode() as usize]
    ));
    ui.label(format!(
        "Clears: {} | Saves: {}",
        save.clear_count(),
        save.save_count()
    ));
    ui.label(format!(
        "EXP {} (max {}) | BP {} (max {})",
        save.player_exp(),
        save.max_exp(),
        save.player_bp(),
        save.max_bp()
    ));
    ui.label(format!(
        "Battles won {} / escaped {}",
        save.battles_won(),
        save.battles_escaped()
    ));
    let range = save.carried_range();
    ui.label(format!(
        "Carried range: rows {}..{} (table {})",
        range.first_row,
        range.first_row as u16 + range.row_count as u16,
        range.table_id
    ));
    false
}

fn show_character(ui: &mut egui::Ui, save: &mut SaveImage) -> bool {
    ui.heading("Character");
    let mut changed = false;
    let mut exp = save.player_exp();
    if ui
        .add(egui::DragValue::new(&mut exp).prefix("EXP "))
        .changed()
    {
        save.set_player_exp(exp);
        changed = true;
    }
    let mut bp = save.player_bp();
    if ui
        .add(egui::DragValue::new(&mut bp).prefix("BP "))
        .changed()
    {
        save.set_player_bp(bp);
        changed = true;
    }
    let mut max_exp = save.max_exp();
    if ui
        .add(egui::DragValue::new(&mut max_exp).prefix("max EXP "))
        .changed()
    {
        save.set_max_exp(max_exp);
        changed = true;
    }
    let mut max_bp = save.max_bp();
    if ui
        .add(egui::DragValue::new(&mut max_bp).prefix("max BP "))
        .changed()
    {
        save.set_max_bp(max_bp);
        changed = true;
    }
    let mut hp = save.hp_bonus();
    if ui
        .add(
            egui::DragValue::new(&mut hp)
                .prefix("HP bonus ")
                .range(0..=255),
        )
        .changed()
    {
        save.set_hp_bonus(hp);
        changed = true;
    }
    let mut mp = save.mp_bonus();
    if ui
        .add(
            egui::DragValue::new(&mut mp)
                .prefix("MP bonus ")
                .range(0..=255),
        )
        .changed()
    {
        save.set_mp_bonus(mp);
        changed = true;
    }
    let mut minutes = save.play_time_minutes();
    if ui
        .add(egui::DragValue::new(&mut minutes).prefix("play time (min) "))
        .changed()
    {
        save.set_play_time_minutes(minutes);
        changed = true;
    }
    let mut companion_hp = save.companion_hp();
    if ui
        .add(egui::DragValue::new(&mut companion_hp).prefix("companion HP "))
        .changed()
    {
        save.set_companion_hp(companion_hp);
        changed = true;
    }
    let mut companion_hp_max = save.companion_hp_max();
    if ui
        .add(egui::DragValue::new(&mut companion_hp_max).prefix("companion HP max "))
        .changed()
    {
        save.set_companion_hp_max(companion_hp_max);
        changed = true;
    }
    changed
}

fn show_inventory(ui: &mut egui::Ui, save: &mut SaveImage, item_filter: &mut String) -> bool {
    ui.heading("Item rows");
    ui.text_edit_singleline(item_filter);
    let filter = item_filter.to_lowercase();
    let mut changed = false;
    egui::Grid::new("rows").striped(true).show(ui, |ui| {
        ui.strong("row");
        ui.strong("id");
        ui.strong("attach");
        ui.strong("qty");
        ui.end_row();
        for i in 0..ITEM_ROW_COUNT {
            let row = save.item_row(i);
            if row.item_id == 0 && filter.is_empty() {
                continue;
            }
            let text = format!(
                "{:3}  {:#04x}  {:4}  {}",
                i, row.item_id, row.attach_slot, row.qty
            );
            if !filter.is_empty() && !text.to_lowercase().contains(&filter) {
                continue;
            }
            ui.label(format!("{i}"));
            let mut id = row.item_id;
            if ui
                .add(egui::DragValue::new(&mut id).hexadecimal(2, false, true))
                .changed()
            {
                save.set_item_row(i, ItemRow { item_id: id, ..row });
                changed = true;
            }
            let mut attach = row.attach_slot as i32;
            if ui
                .add(egui::DragValue::new(&mut attach).range(-1..=10))
                .changed()
            {
                save.set_item_row(
                    i,
                    ItemRow {
                        attach_slot: attach as i8,
                        ..row
                    },
                );
                changed = true;
            }
            let mut qty = row.qty;
            if ui
                .add(egui::DragValue::new(&mut qty).range(0..=65535))
                .changed()
            {
                save.set_item_row(i, ItemRow { qty, ..row });
                changed = true;
            }
            if ui.button("✕").clicked() {
                save.set_item_row(
                    i,
                    ItemRow {
                        item_id: 0,
                        attach_slot: 0,
                        qty: 0,
                    },
                );
                changed = true;
            }
            ui.end_row();
        }
    });
    let mut range = save.carried_range();
    ui.add(
        egui::DragValue::new(&mut range.first_row)
            .prefix("first row ")
            .range(0..=255),
    );
    ui.add(
        egui::DragValue::new(&mut range.row_count)
            .prefix("row count ")
            .range(0..=255),
    );
    if ui.button("apply carried range").clicked() {
        save.set_carried_range(range);
        changed = true;
    }
    changed
}

fn show_weapons(ui: &mut egui::Ui, save: &mut SaveImage) -> bool {
    ui.heading("Weapon consumable loads");
    let mut changed = false;
    egui::Grid::new("weapons").striped(true).show(ui, |ui| {
        ui.strong("weapon");
        ui.strong("primary");
        ui.strong("qty");
        ui.strong("secondary");
        ui.strong("qty");
        ui.strong("uses");
        ui.end_row();
        for i in 0..WEAPON_LOAD_COUNT {
            let load = save.weapon_load(i);
            if load.primary_item_id == 0 && load.secondary_item_id == 0 {
                continue;
            }
            ui.label(format!("{:#04x}", 0x80 + i));
            let mut p = load.primary_item_id;
            if ui
                .add(egui::DragValue::new(&mut p).hexadecimal(2, false, true))
                .changed()
            {
                save.set_weapon_load(
                    i,
                    WeaponLoad {
                        primary_item_id: p,
                        ..load
                    },
                );
                changed = true;
            }
            let mut pq = load.primary_qty;
            if ui.add(egui::DragValue::new(&mut pq)).changed() {
                save.set_weapon_load(
                    i,
                    WeaponLoad {
                        primary_qty: pq,
                        ..load
                    },
                );
                changed = true;
            }
            let mut s = load.secondary_item_id;
            if ui
                .add(egui::DragValue::new(&mut s).hexadecimal(2, false, true))
                .changed()
            {
                save.set_weapon_load(
                    i,
                    WeaponLoad {
                        secondary_item_id: s,
                        ..load
                    },
                );
                changed = true;
            }
            let mut sq = load.secondary_qty;
            if ui.add(egui::DragValue::new(&mut sq)).changed() {
                save.set_weapon_load(
                    i,
                    WeaponLoad {
                        secondary_qty: sq,
                        ..load
                    },
                );
                changed = true;
            }
            let mut uses = save.weapon_use_count(i);
            if ui.add(egui::DragValue::new(&mut uses)).changed() {
                save.set_weapon_use_count(i, uses);
                changed = true;
            }
            ui.end_row();
        }
    });
    changed
}

fn show_pe(ui: &mut egui::Ui, save: &mut SaveImage) -> bool {
    ui.heading("Parasite Energy");
    let mut changed = false;
    egui::Grid::new("pe").striped(true).show(ui, |ui| {
        ui.strong("slot");
        ui.strong("level");
        ui.strong("uses");
        ui.end_row();
        for i in 0..PE_LEVEL_COUNT {
            let mut level = save.pe_level(i);
            ui.label(format!("{i}"));
            if ui
                .add(egui::DragValue::new(&mut level).range(0..=3))
                .changed()
            {
                save.set_pe_level(i, level);
                changed = true;
            }
            if i < PE_USE_COUNT {
                let mut uses = save.pe_use_count(i);
                if ui.add(egui::DragValue::new(&mut uses)).changed() {
                    save.set_pe_use_count(i, uses);
                    changed = true;
                }
            } else {
                ui.label("—");
            }
            ui.end_row();
        }
    });
    changed
}

fn show_progression(ui: &mut egui::Ui, save: &mut SaveImage) -> bool {
    ui.heading("Progression");
    let mut changed = false;
    ui.horizontal(|ui| {
        if ui.button("mark all items seen (0x180)").clicked() {
            save.mark_items_seen(0x180);
            changed = true;
        }
        let mut tiers = save.shop_tiers();
        if ui
            .add(
                egui::DragValue::new(&mut tiers)
                    .hexadecimal(4, false, true)
                    .prefix("shop tiers "),
            )
            .changed()
        {
            save.set_shop_tiers(tiers);
            changed = true;
        }
        let mut stock = save.shop_stock();
        if ui
            .add(
                egui::DragValue::new(&mut stock)
                    .hexadecimal(8, false, true)
                    .prefix("shop stock "),
            )
            .changed()
        {
            save.set_shop_stock(stock);
            changed = true;
        }
    });
    ui.separator();
    ui.label("Collectibles (128)");
    egui::Grid::new("collected").show(ui, |ui| {
        for base in (0..COLLECTED_BIT_WORDS * 32).step_by(16) {
            for bit in base..base + 16 {
                let mut on = save.collected_bit(bit);
                if ui.toggle_value(&mut on, format!("{bit}")).changed() {
                    save.set_collected_bit(bit, on);
                    changed = true;
                }
            }
            ui.end_row();
        }
    });
    ui.separator();
    ui.label("Visit flags (bits 1..5 = stages)");
    let mut flags = save.visit_flags();
    if ui
        .add(egui::DragValue::new(&mut flags).hexadecimal(2, false, true))
        .changed()
    {
        save.set_visit_flags(flags);
        changed = true;
    }
    changed
}

fn show_options(ui: &mut egui::Ui, save: &mut SaveImage) -> bool {
    ui.heading("Options");
    let mut changed = false;
    let mut v = save.vibration_off();
    if ui.checkbox(&mut v, "vibration off").changed() {
        save.set_vibration_off(v);
        changed = true;
    }
    let mut r = save.run_default();
    if ui.checkbox(&mut r, "run by default").changed() {
        save.set_run_default(r);
        changed = true;
    }
    let mut layout = save.button_layout();
    if ui
        .add(
            egui::DragValue::new(&mut layout)
                .prefix("button layout ")
                .range(0..=2),
        )
        .changed()
    {
        save.set_button_layout(layout);
        changed = true;
    }
    let mut mono = save.mono_sound();
    if ui.checkbox(&mut mono, "mono sound").changed() {
        save.set_mono_sound(mono);
        changed = true;
    }
    let mut volume = save.music_volume();
    if ui
        .add(
            egui::DragValue::new(&mut volume)
                .prefix("music volume (0 full .. 3 off) ")
                .range(0..=3),
        )
        .changed()
    {
        save.set_music_volume(volume);
        changed = true;
    }
    let mut cursor = save.cursor_reset();
    if ui.checkbox(&mut cursor, "reset menu cursor").changed() {
        save.set_cursor_reset(cursor);
        changed = true;
    }
    let mut interlace = save.interlaced();
    if ui.checkbox(&mut interlace, "interlaced display").changed() {
        save.set_interlaced(interlace);
        changed = true;
    }
    let mut cheat = save.cheat_mode();
    if ui
        .checkbox(&mut cheat, "cheat mode (nothing is spent)")
        .changed()
    {
        save.set_cheat_mode(cheat);
        changed = true;
    }
    let mut frozen = save.actors_frozen();
    if ui.checkbox(&mut frozen, "actors frozen").changed() {
        save.set_actors_frozen(frozen);
        changed = true;
    }
    changed
}

/// Run the GUI.
pub fn run_gui() -> Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([1180.0, 720.0]),
        ..Default::default()
    };
    eframe::run_native(
        "Parasite Eve II Save Editor",
        options,
        Box::new(|_cc| Ok(Box::new(EditorApp::new()))),
    )
    .map_err(|e| anyhow::anyhow!("eframe error: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::layout::{FILE_SIZE, PLAYER_SECTION};
    use std::fs;

    #[test]
    fn standalone_save_commit_survives_reopen_without_native_windows() {
        let root = std::env::temp_dir().join(format!("pe2-editor-gui-{}", std::process::id()));
        fs::create_dir_all(&root).unwrap();
        for mcs in [false, true] {
            let path = root.join(if mcs { "copy.mcs" } else { "copy.slot" });
            let mut raw = vec![0u8; FILE_SIZE];
            raw[..2].copy_from_slice(b"SC");
            let mut header = vec![0u8; 128];
            header[..2].copy_from_slice(b"SC");
            header[0x0a..0x0a + 20].copy_from_slice(b"BASLUS-01042B1234567");
            let bytes = if mcs {
                [header.clone(), raw].concat()
            } else {
                raw
            };
            fs::write(&path, bytes).unwrap();
            let mut app = EditorApp::new();
            app.load_container(path.to_str().unwrap());
            app.select_slot(if mcs { 1 } else { 0 });
            app.working
                .as_mut()
                .unwrap()
                .edit(|save| save.set_player_bp(4321))
                .unwrap();
            let ctx = egui::Context::default();
            let output = ctx.run(egui::RawInput::default(), |ctx| {
                app.top_panel(ctx);
                app.side_panel(ctx);
                app.central_panel(ctx);
            });
            assert!(!output.shapes.is_empty());
            app.commit();
            assert!(app.status.starts_with("Saved"), "{}", app.status);
            let reopened = CardContainer::open(&path).unwrap();
            let file = reopened.slots().remove(0).1;
            let slot = SlotData::from_bytes(file.data).unwrap();
            assert_eq!(slot.save().unwrap().player_bp(), 4321);
            let offset = PLAYER_SECTION.live_offset + 0x0c;
            assert_eq!(
                i32::from_le_bytes(slot.data[offset..offset + 4].try_into().unwrap()),
                4321
            );
            let (sections, header_ok, summary_ok) = slot.verify();
            assert!(sections.iter().all(|ok| *ok) && header_ok && summary_ok);
            if mcs {
                assert_eq!(&fs::read(&path).unwrap()[..128], header);
            }
        }
        fs::remove_dir_all(root).unwrap();
    }
}
