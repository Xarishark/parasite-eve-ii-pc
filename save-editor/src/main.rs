//! Entry point: GUI by default, CLI subcommands when arguments are given.

use clap::Parser;
use pe2_save_editor::cli;

fn main() -> anyhow::Result<()> {
    let cli = cli::Cli::parse();
    match cli.command {
        Some(_) => cli::run(cli),
        None => pe2_save_editor::gui::run_gui(),
    }
}
