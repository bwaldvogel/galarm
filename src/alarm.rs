use std::time::{Duration, Instant};

use jiff::Zoned;

use crate::timeout::Timeout;

/// The countdown uses the monotonic clock, absolute timeouts the wall clock
pub struct Now {
    pub instant: Instant,
    pub zoned: Zoned,
}

impl Now {
    pub fn current() -> Self {
        Now {
            instant: Instant::now(),
            zoned: Zoned::now(),
        }
    }
}

/// Keeps track of the remaining time and supports pausing in countdown mode
pub struct Alarm {
    timeout: Timeout,
    remaining: Duration,      // countdown mode, as of `started`
    started: Option<Instant>, // None while paused
}

impl Alarm {
    pub fn new(timeout: Timeout, now: &Now) -> Self {
        let remaining = match timeout {
            Timeout::Countdown(duration) => duration,
            Timeout::At(_) => Duration::ZERO,
        };
        Alarm {
            timeout,
            remaining,
            started: Some(now.instant),
        }
    }

    pub fn is_countdown(&self) -> bool {
        matches!(self.timeout, Timeout::Countdown(_))
    }

    pub fn is_paused(&self) -> bool {
        self.is_countdown() && self.started.is_none()
    }

    pub fn left(&self, now: &Now) -> Duration {
        match &self.timeout {
            Timeout::At(end) => now
                .zoned
                .duration_until(end)
                .try_into()
                .unwrap_or(Duration::ZERO),
            Timeout::Countdown(_) => match self.started {
                Some(started) => self.remaining.saturating_sub(now.instant - started),
                None => self.remaining,
            },
        }
    }

    pub fn toggle_pause(&mut self, now: &Now) {
        if !self.is_countdown() {
            return;
        }
        match self.started.take() {
            Some(started) => self.remaining = self.remaining.saturating_sub(now.instant - started),
            None => self.started = Some(now.instant),
        }
    }

    /// Adds to the remaining time. Once the alarm went off, it counts from now on.
    pub fn extend_time(&mut self, by: Duration, now: &Now) {
        let left = self.left(now);
        match &mut self.timeout {
            Timeout::At(end) => {
                if let Ok(extended) = now.zoned.checked_add(left + by) {
                    *end = extended;
                }
            }
            Timeout::Countdown(_) => {
                self.remaining = left + by;
                if self.started.is_some() {
                    self.started = Some(now.instant);
                }
            }
        }
    }

    /// Shown as tooltip of the tray icon, e.g. "tea is ready: 00:04:59 (@17:25:30)"
    pub fn status(&self, message: &str, now: &Now) -> String {
        let left = self.left(now);
        let status = format!("{message}: {}", format_duration(left));
        if self.is_paused() {
            return status + " (paused)";
        }
        let end = match &self.timeout {
            Timeout::At(end) => end.clone(),
            Timeout::Countdown(_) => match now.zoned.checked_add(left) {
                Ok(end) => end,
                Err(_) => return status,
            },
        };
        let format = if end.date() == now.zoned.date() {
            "%H:%M:%S"
        } else {
            "%Y-%m-%d %H:%M:%S"
        };
        format!("{status} (@{})", end.strftime(format))
    }
}

/// Rounds up, so the countdown doesn't show 00:00:00 before the alarm fires
fn format_duration(d: Duration) -> String {
    let seconds = d.as_secs() + u64::from(d.subsec_nanos() > 0);
    let days = seconds / (24 * 3600);
    let hms = format!(
        "{:02}:{:02}:{:02}",
        seconds / 3600 % 24,
        seconds / 60 % 60,
        seconds % 60
    );
    match days {
        0 => hms,
        1 => format!("1 day {hms}"),
        _ => format!("{days} days {hms}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use jiff::civil::date;
    use jiff::tz::TimeZone;

    fn start() -> Now {
        Now {
            instant: Instant::now(),
            zoned: date(2026, 10, 27)
                .at(8, 0, 0, 0)
                .to_zoned(TimeZone::UTC)
                .unwrap(),
        }
    }

    fn after(start: &Now, d: Duration) -> Now {
        Now {
            instant: start.instant + d,
            zoned: start.zoned.checked_add(d).unwrap(),
        }
    }

    const MINUTE: Duration = Duration::from_secs(60);
    const HOUR: Duration = Duration::from_secs(3600);

    #[test]
    fn format_durations() {
        let tests = [
            (Duration::ZERO, "00:00:00"),
            (Duration::from_millis(300), "00:00:01"),
            (Duration::from_secs(59), "00:00:59"),
            (HOUR + 2 * MINUTE + Duration::from_secs(3), "01:02:03"),
            (24 * HOUR, "1 day 00:00:00"),
            (50 * HOUR + Duration::from_secs(5), "2 days 02:00:05"),
        ];
        for (d, expected) in tests {
            assert_eq!(format_duration(d), expected, "duration: {d:?}");
        }
    }

    #[test]
    fn countdown() {
        let start = start();
        let mut alarm = Alarm::new(Timeout::Countdown(5 * MINUTE), &start);

        let now = after(&start, MINUTE);
        assert_eq!(alarm.left(&now), 4 * MINUTE);
        assert_eq!(alarm.status("tea", &now), "tea: 00:04:00 (@08:05:00)");

        alarm.toggle_pause(&now);
        let later = after(&start, HOUR);
        assert!(alarm.is_paused());
        assert_eq!(alarm.left(&later), 4 * MINUTE);
        assert_eq!(alarm.status("tea", &later), "tea: 00:04:00 (paused)");

        alarm.toggle_pause(&later);
        assert_eq!(alarm.left(&after(&start, HOUR + MINUTE)), 3 * MINUTE);
        assert_eq!(alarm.left(&after(&start, 2 * HOUR)), Duration::ZERO);
    }

    #[test]
    fn fixed_end_time() {
        let start = start();
        let end = start.zoned.checked_add(26 * HOUR).unwrap();
        let mut alarm = Alarm::new(Timeout::At(end), &start);

        assert_eq!(
            alarm.status("call", &start),
            "call: 1 day 02:00:00 (@2026-10-28 10:00:00)"
        );

        alarm.toggle_pause(&start);
        assert!(!alarm.is_paused());
        assert_eq!(alarm.left(&after(&start, 27 * HOUR)), Duration::ZERO);
    }

    #[test]
    fn extend_time_of_countdown() {
        let start = start();
        let mut alarm = Alarm::new(Timeout::Countdown(5 * MINUTE), &start);

        alarm.extend_time(MINUTE, &after(&start, MINUTE));
        assert_eq!(alarm.left(&after(&start, 2 * MINUTE)), 4 * MINUTE);

        let paused_at = after(&start, 2 * MINUTE);
        alarm.toggle_pause(&paused_at);
        alarm.extend_time(5 * MINUTE, &after(&start, HOUR));
        assert!(alarm.is_paused());
        assert_eq!(alarm.left(&after(&start, 2 * HOUR)), 9 * MINUTE);
    }

    #[test]
    fn extend_time_after_alarm_went_off() {
        let start = start();
        let later = after(&start, HOUR);

        let mut countdown = Alarm::new(Timeout::Countdown(5 * MINUTE), &start);
        countdown.extend_time(MINUTE, &later);
        assert_eq!(countdown.left(&later), MINUTE);

        let end = start.zoned.checked_add(5 * MINUTE).unwrap();
        let mut fixed = Alarm::new(Timeout::At(end), &start);
        fixed.extend_time(MINUTE, &later);
        assert_eq!(fixed.left(&later), MINUTE);
        assert_eq!(fixed.status("call", &later), "call: 00:01:00 (@09:01:00)");
    }

    #[test]
    fn extend_time_of_fixed_end_time() {
        let start = start();
        let end = start.zoned.checked_add(HOUR).unwrap();
        let mut alarm = Alarm::new(Timeout::At(end), &start);

        alarm.extend_time(15 * MINUTE, &after(&start, MINUTE));
        assert_eq!(alarm.status("call", &start), "call: 01:15:00 (@09:15:00)");
    }
}
