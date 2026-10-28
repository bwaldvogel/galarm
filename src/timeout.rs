use std::fmt;
use std::time::Duration;

use jiff::Zoned;
use jiff::civil::Date;

#[derive(Debug, Clone, PartialEq)]
pub enum Timeout {
    Countdown(Duration),
    At(Zoned),
}

#[derive(Debug, PartialEq)]
pub struct InvalidTimeout;

impl fmt::Display for InvalidTimeout {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str("provide a valid timeout. see --help")
    }
}

impl std::error::Error for InvalidTimeout {}

/// Parses relative timeouts like "5" (minutes), "1.5d", "1,2m", "1:12m" or "30sec"
/// and absolute ones like "@17", "@12:30" or "@9pm".
pub fn parse(s: &str, now: &Zoned) -> Result<Timeout, InvalidTimeout> {
    match s.strip_prefix('@') {
        Some(time) => parse_absolute(time, now).map(Timeout::At),
        None => parse_relative(s).map(Timeout::Countdown),
    }
}

fn parse_relative(s: &str) -> Result<Duration, InvalidTimeout> {
    let (whole, rest) = digits(s, 5).ok_or(InvalidTimeout)?;
    let mut value: f64 = whole.parse().map_err(|_| InvalidTimeout)?;

    let mut chars = rest.chars();
    let unit = match chars.next() {
        Some(separator @ (':' | ',' | '.')) => {
            let (fraction, unit) = digits(chars.as_str(), 6).ok_or(InvalidTimeout)?;
            if separator == ':' {
                let seconds: u32 = fraction.parse().map_err(|_| InvalidTimeout)?;
                if seconds >= 60 {
                    return Err(InvalidTimeout);
                }
                value += f64::from(seconds) / 60.0;
            } else {
                value += format!("0.{fraction}")
                    .parse::<f64>()
                    .map_err(|_| InvalidTimeout)?;
            }
            unit
        }
        _ => rest,
    };

    let unit_millis = match unit.to_ascii_lowercase().as_str() {
        "d" => 24 * 3600 * 1000,
        "h" => 3600 * 1000,
        "m" | "min" | "" => 60 * 1000,
        "s" | "sec" => 1000,
        _ => return Err(InvalidTimeout),
    };
    Ok(Duration::from_millis(
        (value * f64::from(unit_millis)).round() as u64,
    ))
}

fn parse_absolute(s: &str, now: &Zoned) -> Result<Zoned, InvalidTimeout> {
    let (hour, mut rest) = digits(s, 2).ok_or(InvalidTimeout)?;
    let mut hour: i8 = hour.parse().map_err(|_| InvalidTimeout)?;
    let mut minute_and_second = [0i8; 2];
    for value in &mut minute_and_second {
        let Some(after_colon) = rest.strip_prefix(':') else {
            break;
        };
        let (digits, after_digits) = digits(after_colon, 2).ok_or(InvalidTimeout)?;
        *value = digits.parse().map_err(|_| InvalidTimeout)?;
        if *value >= 60 {
            return Err(InvalidTimeout);
        }
        rest = after_digits;
    }
    let [minute, second] = minute_and_second;

    match parse_am_pm(rest)? {
        Some(pm) => {
            if !(1..=12).contains(&hour) {
                return Err(InvalidTimeout);
            }
            hour %= 12;
            if pm {
                hour += 12;
            }
        }
        None if hour == 24 && minute == 0 && second == 0 => {}
        None if hour >= 24 => return Err(InvalidTimeout),
        None => {}
    }

    let at = |date: Date, hour| {
        date.at(hour, minute, second, 0)
            .to_zoned(now.time_zone().clone())
            .map_err(|_| InvalidTimeout)
    };
    let tomorrow = || now.date().tomorrow().map_err(|_| InvalidTimeout);
    // midnight at the end of today, even if it's exactly midnight now
    if hour == 24 {
        return at(tomorrow()?, 0);
    }
    let end = at(now.date(), hour)?;
    // if end is before now, assume the next day is meant.
    // Going through the date instead of adding 24h keeps the wall clock time across DST changes
    if end < *now {
        return at(tomorrow()?, hour);
    }
    Ok(end)
}

/// Accepts "", "am", "pm", "a.m.", "p.m." and the like; returns whether it's pm
fn parse_am_pm(s: &str) -> Result<Option<bool>, InvalidTimeout> {
    if s.is_empty() {
        return Ok(None);
    }
    let s = s.to_ascii_lowercase();
    let (pm, rest) = match s.split_at_checked(1) {
        Some(("a", rest)) => (false, rest),
        Some(("p", rest)) => (true, rest),
        _ => return Err(InvalidTimeout),
    };
    let rest = rest.strip_prefix('.').unwrap_or(rest);
    let rest = rest.strip_prefix('m').ok_or(InvalidTimeout)?;
    match rest {
        "" | "." => Ok(Some(pm)),
        _ => Err(InvalidTimeout),
    }
}

/// Splits off 1 to `max` leading ASCII digits
fn digits(s: &str, max: usize) -> Option<(&str, &str)> {
    let len = s.bytes().take_while(u8::is_ascii_digit).count();
    (1..=max).contains(&len).then(|| s.split_at(len))
}

#[cfg(test)]
mod tests {
    use super::*;
    use jiff::civil::date;
    use jiff::tz::TimeZone;

    fn now() -> Zoned {
        date(2026, 10, 27)
            .at(8, 0, 0, 0)
            .to_zoned(TimeZone::UTC)
            .unwrap()
    }

    #[test]
    fn relative_timeouts() {
        let tests = [
            ("5", 5 * 60_000),
            ("19", 19 * 60_000),
            ("5m", 5 * 60_000),
            ("5M", 5 * 60_000),
            ("3min", 3 * 60_000),
            ("5s", 5_000),
            ("30sec", 30_000),
            ("9.5s", 9_500),
            ("7h", 7 * 3_600_000),
            ("1d", 24 * 3_600_000),
            ("6.5m", 6 * 60_000 + 30_000),
            ("1,2m", 72_000),
            ("1:12m", 72_000),
            ("1.25h", 3_600_000 + 15 * 60_000),
            ("1,5d", 36 * 3_600_000),
            ("1.5d", 36 * 3_600_000),
            ("1:30d", 36 * 3_600_000),
            ("0:30m", 30_000),
        ];
        for (input, millis) in tests {
            assert_eq!(
                parse(input, &now()),
                Ok(Timeout::Countdown(Duration::from_millis(millis))),
                "input: {input}"
            );
        }
    }

    #[test]
    fn absolute_timeouts() {
        let today = |h, m, s| {
            date(2026, 10, 27)
                .at(h, m, s, 0)
                .to_zoned(TimeZone::UTC)
                .unwrap()
        };
        let tomorrow = |h, m, s| {
            date(2026, 10, 28)
                .at(h, m, s, 0)
                .to_zoned(TimeZone::UTC)
                .unwrap()
        };

        let tests = [
            ("@17", today(17, 0, 0)),
            ("@17:25", today(17, 25, 0)),
            ("@17:25:30", today(17, 25, 30)),
            ("@9am", today(9, 0, 0)),
            ("@9pm", today(21, 0, 0)),
            ("@9p.m.", today(21, 0, 0)),
            ("@9:30PM", today(21, 30, 0)),
            ("@12pm", today(12, 0, 0)),
            ("@12am", tomorrow(0, 0, 0)),
            ("@8", today(8, 0, 0)),
            ("@7", tomorrow(7, 0, 0)),
            ("@0", tomorrow(0, 0, 0)),
            ("@24", tomorrow(0, 0, 0)),
            ("@24:00", tomorrow(0, 0, 0)),
            ("@24:00:00", tomorrow(0, 0, 0)),
        ];
        for (input, end) in tests {
            assert_eq!(parse(input, &now()), Ok(Timeout::At(end)), "input: {input}");
        }
    }

    #[test]
    fn absolute_timeout_at_midnight() {
        let midnight = date(2026, 10, 27)
            .at(0, 0, 0, 0)
            .to_zoned(TimeZone::UTC)
            .unwrap();
        let tomorrow = date(2026, 10, 28)
            .at(0, 0, 0, 0)
            .to_zoned(TimeZone::UTC)
            .unwrap();

        assert_eq!(parse("@0", &midnight), Ok(Timeout::At(midnight.clone())));
        assert_eq!(parse("@24", &midnight), Ok(Timeout::At(tomorrow)));
    }

    #[test]
    fn absolute_timeout_across_dst_change() {
        let berlin = TimeZone::get("Europe/Berlin").unwrap();
        // clocks go back one hour in the night to 2026-10-25
        let now = date(2026, 10, 24)
            .at(23, 0, 0, 0)
            .to_zoned(berlin.clone())
            .unwrap();
        let end = date(2026, 10, 25).at(22, 0, 0, 0).to_zoned(berlin).unwrap();

        assert_eq!(parse("@22", &now), Ok(Timeout::At(end)));
    }

    #[test]
    fn invalid_timeouts() {
        let tests = [
            "",
            "abc",
            "5x",
            "1:60m",
            "-5",
            "123456",
            "1.",
            "@",
            "@25",
            "@24:01",
            "@24:00:01",
            "@0am",
            "@13pm",
            "@9.5pm",
            "@12:60",
            "@9p",
            "@9pmx",
            "@123",
            "@24pm",
        ];
        for input in tests {
            assert_eq!(parse(input, &now()), Err(InvalidTimeout), "input: {input}");
        }
    }
}
