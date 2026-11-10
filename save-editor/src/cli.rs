//! Command-line interface: container inspection, typed edits and JSON transfer.

use std::path::PathBuf;

use anyhow::{bail, Context, Result};
use clap::{Args, Parser, Subcommand};

use crate::card::{write_slot_file, CardContainer};
use crate::edit::SlotData;
use crate::save::{SaveImage, SaveTransfer, ITEM_ROW_COUNT, WEAPON_LOAD_COUNT};

#[derive(Parser)]
pub struct Cli {
    #[command(subcommand)]
    pub command: Option<Command>,
}

#[derive(Subcommand)]
pub enum Command {
    /// List PE2 saves in a container.
    List { path: PathBuf },
    /// Show one slot's contents (add --verbose for all typed fields).
    Inspect {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
        #[arg(long)]
        verbose: bool,
    },
    /// Verify every live checksum of one slot.
    Verify {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
    },
    /// Edit typed fields of one slot; unknown bytes are preserved.
    Edit(EditArgs),
    /// Export one slot's typed fields as JSON.
    Export {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
        #[arg(long)]
        out: PathBuf,
    },
    /// Apply a JSON transfer onto one slot (None fields are skipped).
    Import {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
        #[arg(long)]
        input: PathBuf,
    },
    /// Export one slot's raw 0x2000-byte file.
    Extract {
        path: PathBuf,
        #[arg(long)]
        slot: Option<usize>,
        #[arg(long)]
        out: PathBuf,
    },
}

#[derive(Args)]
pub struct EditArgs {
    pub path: PathBuf,
    #[arg(long)]
    slot: Option<usize>,
    /// Field=value pairs; run `edit --help-fields` style listing via `fields`.
    #[arg(long = "set")]
    sets: Vec<String>,
    /// Print the supported field names instead of editing.
    #[arg(long)]
    fields: bool,
}

fn resolve_slot(
    container: &CardContainer,
    slot: Option<usize>,
) -> Result<(usize, crate::card::SlotFile)> {
    let slots = container.slots();
    if slots.is_empty() {
        bail!("no PE2 save files found");
    }
    let available: Vec<usize> = slots.iter().map(|(i, _)| *i).collect();
    match slot {
        Some(index) => slots.into_iter().find(|(i, _)| *i == index).ok_or_else(|| {
            anyhow::anyhow!("slot {} not found (available: {:?})", index, available)
        }),
        None if slots.len() == 1 => Ok(slots.into_iter().next().unwrap()),
        None => bail!(
            "multiple slots found; pass --slot (available: {:?})",
            available
        ),
    }
}

fn open_slot(path: &PathBuf, slot: Option<usize>) -> Result<(CardContainer, usize, SlotData)> {
    let container = CardContainer::open(path).context("opening container")?;
    let (index, file) = resolve_slot(&container, slot)?;
    let data = SlotData::from_bytes(file.data.clone())?;
    Ok((container, index, data))
}

/// Print the human-readable summary shown by `list` and `inspect`.
pub fn describe(save: &SaveImage) -> String {
    format!(
        "save #{} at {} | mode {} | {} min | EXP {} BP {} | battles {}/{}",
        save.save_number(),
        save.save_point_label(),
        crate::save::GAME_MODE_LABELS[save.game_mode() as usize],
        save.play_time_minutes(),
        save.player_exp(),
        save.player_bp(),
        save.battles_won(),
        save.battles_escaped(),
    )
}

fn carried_item_count(save: &SaveImage) -> usize {
    let range = save.carried_range();
    let mut count = 0;
    for i in 0..range.row_count as usize {
        let index = range.first_row as usize + i;
        if index < ITEM_ROW_COUNT && save.item_row(index).item_id != 0 {
            count += 1;
        }
    }
    count
}

pub fn run(cli: Cli) -> Result<()> {
    match cli.command.expect("main dispatches None to the GUI") {
        Command::List { path } => {
            let container = CardContainer::open(&path).context("opening container")?;
            let slots = container.slots();
            if slots.is_empty() {
                println!("no PE2 save files in {}", path.display());
            }
            for (index, file) in &slots {
                let data = SlotData::from_bytes(file.data.clone())?;
                let save = data.save()?;
                let (sections, header, summary) = data.verify();
                let status = if sections.iter().all(|ok| *ok) && header && summary {
                    "OK"
                } else {
                    "CHECKSUM MISMATCH"
                };
                println!(
                    "[{}] {} — {} [{}]",
                    index,
                    file.filename,
                    describe(&save),
                    status
                );
            }
        }
        Command::Inspect {
            path,
            slot,
            verbose,
        } => {
            let (_, index, data) = open_slot(&path, slot)?;
            let save = data.save()?;
            println!("slot {}", index);
            println!("{}", describe(&save));
            let range = save.carried_range();
            println!(
                "carried items: {} in rows {}..{}",
                carried_item_count(&save),
                range.first_row,
                range.first_row as u16 + range.row_count as u16
            );
            println!(
                "companion: type {} variant {} hp {}/{}",
                save.companion_type(),
                save.companion_variant(),
                save.companion_hp(),
                save.companion_hp_max()
            );
            println!(
                "clears {} | saves {} | shop tiers {:#x}",
                save.clear_count(),
                save.save_count(),
                save.shop_tiers()
            );
            if verbose {
                println!("options: vibration_off {} run {} layout {} mono {} music {} cursor_reset {} interlace {} cheat {}",
                    save.vibration_off(), save.run_default(), save.button_layout(), save.mono_sound(),
                    save.music_volume(), save.cursor_reset(), save.interlaced(), save.cheat_mode());
                println!(
                    "hpBonus {} mpBonus {} maxExp {} maxBp {}",
                    save.hp_bonus(),
                    save.mp_bonus(),
                    save.max_exp(),
                    save.max_bp()
                );
                println!("carried rows:");
                for i in 0..range.row_count as usize {
                    let index = range.first_row as usize + i;
                    if index < ITEM_ROW_COUNT {
                        let row = save.item_row(index);
                        if row.item_id != 0 {
                            println!(
                                "  row {:3}: id {:#04x} attach {:3} qty {}",
                                index, row.item_id, row.attach_slot, row.qty
                            );
                        }
                    }
                }
                println!("weapon loads (id, primary, secondary):");
                for i in 0..WEAPON_LOAD_COUNT {
                    let load = save.weapon_load(i);
                    if load.primary_item_id != 0 || load.secondary_item_id != 0 {
                        println!(
                            "  weapon {:#04x}: {:#04x} x{:<3} / {:#04x} x{}",
                            0x80 + i,
                            load.primary_item_id,
                            load.primary_qty,
                            load.secondary_item_id,
                            load.secondary_qty
                        );
                    }
                }
                println!(
                    "PE levels: {:?}",
                    (0..crate::save::PE_LEVEL_COUNT)
                        .map(|i| save.pe_level(i))
                        .collect::<Vec<_>>()
                );
            }
        }
        Command::Verify { path, slot } => {
            let (_, index, data) = open_slot(&path, slot)?;
            let (sections, header, summary) = data.verify();
            let names = [
                "save",
                "player",
                "akropolis",
                "dryfield",
                "dryfield-night",
                "mine-shelter",
                "neo-ark",
                "nibble",
            ];
            let mut all_ok = header && summary;
            for (ok, name) in sections.iter().zip(names) {
                println!(
                    "section {:<16}: {}",
                    name,
                    if *ok { "OK" } else { "MISMATCH" }
                );
                all_ok &= *ok;
            }
            println!(
                "header checksum : {}",
                if header { "OK" } else { "MISMATCH" }
            );
            println!(
                "section summary : {}",
                if summary { "OK" } else { "MISMATCH" }
            );
            if !all_ok {
                bail!("slot {} has checksum mismatches", index);
            }
        }
        Command::Edit(args) => run_edit(args)?,
        Command::Export { path, slot, out } => {
            let (_, _, data) = open_slot(&path, slot)?;
            let transfer = data.save()?.to_transfer();
            std::fs::write(&out, serde_json::to_string_pretty(&transfer)?)
                .with_context(|| format!("writing {}", out.display()))?;
            println!("exported to {}", out.display());
        }
        Command::Import { path, slot, input } => {
            let json = std::fs::read_to_string(&input)
                .with_context(|| format!("reading {}", input.display()))?;
            let transfer: SaveTransfer =
                serde_json::from_str(&json).context("parsing transfer JSON")?;
            let (mut container, index, mut data) = open_slot(&path, slot)?;
            data.edit(|save| save.apply_transfer(&transfer))
                .expect("edit cannot fail");
            container.write_slot(index, &data.data)?;
            container.save_to_path(&path)?;
            println!("imported into slot {}", index);
        }
        Command::Extract { path, slot, out } => {
            let (_, _, data) = open_slot(&path, slot)?;
            write_slot_file(&out, &data.data)?;
            println!("extracted to {}", out.display());
        }
    }
    Ok(())
}

/// Supported `--set field=value` names for the edit command.
fn apply_field(save: &mut SaveImage, field: &str, value: &str) -> Result<()> {
    macro_rules! int_field {
        ($name:literal, $setter:ident, $ty:ty) => {
            if field == $name {
                let v: $ty = value.parse().context(concat!("bad value for ", $name))?;
                save.$setter(v);
                return Ok(());
            }
        };
    }
    macro_rules! bool_field {
        ($name:literal, $setter:ident) => {
            if field == $name {
                let v = matches!(value, "1" | "true" | "yes" | "on");
                save.$setter(v);
                return Ok(());
            }
        };
    }
    int_field!("play-time", set_play_time_minutes, u16);
    int_field!("exp", set_player_exp, i32);
    int_field!("bp", set_player_bp, i32);
    int_field!("max-exp", set_max_exp, i32);
    int_field!("max-bp", set_max_bp, i32);
    int_field!("hp-bonus", set_hp_bonus, u8);
    int_field!("mp-bonus", set_mp_bonus, u8);
    int_field!("battles-won", set_battles_won, u16);
    int_field!("battles-escaped", set_battles_escaped, u16);
    int_field!("companion-hp", set_companion_hp, i16);
    int_field!("companion-hp-max", set_companion_hp_max, i16);
    int_field!("shop-tiers", set_shop_tiers, u32);
    int_field!("shop-stock", set_shop_stock, u32);
    int_field!("save-number", set_save_number, i8);
    int_field!("save-point", set_save_point, u8);
    int_field!("clear-count", set_clear_count, i8);
    int_field!("game-mode", set_game_mode, u8);
    int_field!("save-count", set_save_count, u8);
    int_field!("replay-rank", set_replay_rank, i8);
    int_field!("companion-type", set_companion_type, i8);
    int_field!("companion-variant", set_companion_variant, i8);
    int_field!("character-id", set_character_id, i8);
    int_field!("button-layout", set_button_layout, u8);
    int_field!("music-volume", set_music_volume, u8);
    bool_field!("vibration-off", set_vibration_off);
    bool_field!("run", set_run_default);
    bool_field!("mono", set_mono_sound);
    bool_field!("cursor-reset", set_cursor_reset);
    bool_field!("interlace", set_interlaced);
    bool_field!("cheat", set_cheat_mode);
    bool_field!("freeze-actors", set_actors_frozen);
    if field == "mark-items-seen" {
        let max: usize = value.parse().context("bad value for mark-items-seen")?;
        save.mark_items_seen(max);
        return Ok(());
    }
    bail!("unknown field {:?}; supported: play-time, exp, bp, max-exp, max-bp, hp-bonus, mp-bonus, battles-won, battles-escaped, companion-hp, companion-hp-max, shop-tiers, shop-stock, save-number, save-point, clear-count, game-mode, save-count, replay-rank, companion-type, companion-variant, character-id, button-layout, music-volume, vibration-off, run, mono, cursor-reset, interlace, cheat, freeze-actors, mark-items-seen", field)
}

fn run_edit(args: EditArgs) -> Result<()> {
    if args.fields {
        println!("play-time exp bp max-exp max-bp hp-bonus mp-bonus battles-won battles-escaped");
        println!("companion-hp companion-hp-max shop-tiers shop-stock save-number save-point");
        println!("clear-count game-mode save-count replay-rank companion-type companion-variant");
        println!("character-id button-layout music-volume vibration-off run mono cursor-reset");
        println!("interlace cheat freeze-actors mark-items-seen");
        return Ok(());
    }
    if args.sets.is_empty() {
        bail!("nothing to set; pass --set field=value (repeatable) or --fields for the list");
    }
    let (mut container, index, mut data) = open_slot(&args.path, args.slot)?;
    let sets = args.sets.clone();
    // Validate every pair against a throwaway image first so failures never
    // partially apply or persist.
    {
        let probe = SaveImage::blank();
        let mut _probe = probe;
        for pair in &sets {
            let (field, value) = pair
                .split_once('=')
                .ok_or_else(|| anyhow::anyhow!("expected field=value, got {:?}", pair))?;
            apply_field(&mut _probe, field, value)?;
        }
    }
    data.edit(|save| {
        for pair in &sets {
            let (field, value) = pair.split_once('=').expect("validated above");
            apply_field(save, field, value).expect("validated above");
        }
    })?;
    container.write_slot(index, &data.data)?;
    container.save_to_path(&args.path)?;
    println!("edited slot {}", index);
    Ok(())
}
