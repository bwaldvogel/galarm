use std::fmt;

pub const USAGE: &str = "\
Usage: galarm [OPTIONS] TIMEOUT [MESSAGE]

Options:
  -q, --quiet     do not play sounds
  -v, --verbose   be verbose
  -h, --help      print this help and exit
      --version   print application version and exit

Possible timeout values:
  5s        5 seconds
  30sec     30 seconds
  6.5m      6:30 minutes
  3min      3 minutes
  19        19 minutes
  7h        7 hours
  1,5d      36 hours
  @17       at 17:00 o'clock
  @17:25    at 17:25 o'clock
  @9:30pm   at 21:30 o'clock";

const DEFAULT_MESSAGE: &str = "alarm";

#[derive(Debug, PartialEq)]
pub enum Cli {
    Help,
    Version,
    Run(Options),
}

#[derive(Debug, Default, PartialEq)]
pub struct Options {
    pub quiet: bool,
    pub verbose: bool,
    pub timeout: String,
    pub message: String,
}

#[derive(Debug, PartialEq)]
pub enum CliError {
    UnknownOption(String),
    MissingTimeout,
}

impl fmt::Display for CliError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            CliError::UnknownOption(option) => write!(f, "unknown option: {option}. see --help"),
            CliError::MissingTimeout => f.write_str("provide a valid timeout. see --help"),
        }
    }
}

impl std::error::Error for CliError {}

/// Options have to precede the timeout, everything after it is the message
pub fn parse(args: impl IntoIterator<Item = String>) -> Result<Cli, CliError> {
    let mut options = Options::default();
    let mut args = args.into_iter();
    let timeout = loop {
        let Some(arg) = args.next() else {
            return Err(CliError::MissingTimeout);
        };
        match arg.as_str() {
            "--" => break args.next().ok_or(CliError::MissingTimeout)?,
            "--quiet" => options.quiet = true,
            "--verbose" => options.verbose = true,
            "--help" => return Ok(Cli::Help),
            "--version" => return Ok(Cli::Version),
            long if long.starts_with("--") => return Err(CliError::UnknownOption(arg)),
            short if short.starts_with('-') && short.len() > 1 => {
                for flag in short[1..].chars() {
                    match flag {
                        'q' => options.quiet = true,
                        'v' => options.verbose = true,
                        'h' => return Ok(Cli::Help),
                        _ => return Err(CliError::UnknownOption(format!("-{flag}"))),
                    }
                }
            }
            _ => break arg,
        }
    };
    options.timeout = timeout;
    options.message = args.collect::<Vec<_>>().join(" ");
    if options.message.is_empty() {
        options.message = DEFAULT_MESSAGE.into();
    }
    Ok(Cli::Run(options))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse_args(args: &str) -> Result<Cli, CliError> {
        parse(args.split_whitespace().map(String::from))
    }

    fn run(timeout: &str, message: &str) -> Options {
        Options {
            timeout: timeout.into(),
            message: message.into(),
            ..Default::default()
        }
    }

    #[test]
    fn timeout_and_message() {
        assert_eq!(parse_args("5"), Ok(Cli::Run(run("5", "alarm"))));
        assert_eq!(
            parse_args("5m tea is ready"),
            Ok(Cli::Run(run("5m", "tea is ready")))
        );
        assert_eq!(parse_args("5 -q"), Ok(Cli::Run(run("5", "-q"))));
        assert_eq!(
            parse_args("-- @12 -5 degrees"),
            Ok(Cli::Run(run("@12", "-5 degrees")))
        );
    }

    #[test]
    fn options() {
        let all = Options {
            quiet: true,
            verbose: true,
            ..run("5", "tea")
        };
        assert_eq!(parse_args("-q -v 5 tea"), Ok(Cli::Run(all)));

        let all = Options {
            quiet: true,
            verbose: true,
            ..run("5", "tea")
        };
        assert_eq!(parse_args("--quiet --verbose 5 tea"), Ok(Cli::Run(all)));

        let combined = Options {
            quiet: true,
            verbose: true,
            ..run("5", "alarm")
        };
        assert_eq!(parse_args("-qv 5"), Ok(Cli::Run(combined)));
    }

    #[test]
    fn help_and_version() {
        assert_eq!(parse_args("-h"), Ok(Cli::Help));
        assert_eq!(parse_args("-q --help 5"), Ok(Cli::Help));
        assert_eq!(parse_args("--version"), Ok(Cli::Version));
    }

    #[test]
    fn errors() {
        assert_eq!(parse_args(""), Err(CliError::MissingTimeout));
        assert_eq!(parse_args("-q"), Err(CliError::MissingTimeout));
        assert_eq!(parse_args("--"), Err(CliError::MissingTimeout));
        assert_eq!(
            parse_args("-x 5"),
            Err(CliError::UnknownOption("-x".into()))
        );
        assert_eq!(
            parse_args("-qx 5"),
            Err(CliError::UnknownOption("-x".into()))
        );
        assert_eq!(
            parse_args("--foo 5"),
            Err(CliError::UnknownOption("--foo".into()))
        );
    }
}
