//! Body markup of the notification, see
//! https://specifications.freedesktop.org/notification-spec/latest/markup.html

const SCHEMES: [&str; 3] = ["https://", "http://", "ftp://"];

/// Escapes the message and turns web links into hyperlinks
pub fn message(message: &str) -> String {
    let mut markup = String::from("<b>");
    let mut rest = message;
    while let Some((start, end)) = find_link(rest) {
        let link = escape(&rest[start..end]);
        markup += &escape(&rest[..start]);
        markup += &format!("<a href='{link}'>{link}</a>");
        rest = &rest[end..];
    }
    markup += &escape(rest);
    markup + "</b>"
}

fn escape(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('\'', "&apos;")
        .replace('"', "&quot;")
}

/// Returns the byte range of the first link. Trailing punctuation is not part of
/// the link, so "see https://example.com." works as expected.
fn find_link(s: &str) -> Option<(usize, usize)> {
    // ASCII lowercasing keeps the byte offsets
    let lower = s.to_ascii_lowercase();
    let mut search_from = 0;
    loop {
        let (start, scheme) = SCHEMES
            .iter()
            .filter_map(|scheme| Some((search_from + lower[search_from..].find(scheme)?, scheme)))
            .min()?;
        search_from = start + scheme.len();

        let starts_word = !s[..start].ends_with(|c: char| c.is_alphanumeric() || c == '_');
        let end = s[start..]
            .find(char::is_whitespace)
            .map_or(s.len(), |len| start + len);
        let link = s[start..end].trim_end_matches(['.', ',', ';', ':', '!', '?', ')']);
        if starts_word && link.len() > scheme.len() {
            return Some((start, start + link.len()));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn messages() {
        let tests = [
            ("tea is ready", "<b>tea is ready</b>"),
            ("salt & pepper <3", "<b>salt &amp; pepper &lt;3</b>"),
            (
                "see https://example.com/a?b=1&c=2.",
                "<b>see <a href='https://example.com/a?b=1&amp;c=2'>https://example.com/a?b=1&amp;c=2</a>.</b>",
            ),
            (
                "HTTP://A.example (and ftp://b.example)",
                "<b><a href='HTTP://A.example'>HTTP://A.example</a> (and <a href='ftp://b.example'>ftp://b.example</a>)</b>",
            ),
            ("it's \"quoted\"", "<b>it&apos;s &quot;quoted&quot;</b>"),
            (
                "no link: https:// or xhttp://example.com",
                "<b>no link: https:// or xhttp://example.com</b>",
            ),
            (
                "ümlaut https://ä.example",
                "<b>ümlaut <a href='https://ä.example'>https://ä.example</a></b>",
            ),
        ];
        for (input, expected) in tests {
            assert_eq!(message(input), expected, "input: {input}");
        }
    }
}
