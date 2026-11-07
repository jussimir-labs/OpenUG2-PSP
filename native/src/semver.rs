//! SemVer 2.0 versions of the beta releases (`vX.Y.Z`, `vX.Y.Z-rc.N`).
use std::cmp::Ordering;

#[derive(Clone, Debug, PartialEq, Eq)]
enum Ident {
    Num(u64),
    Text(String),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Version {
    core: [u64; 3],
    pre: Vec<Ident>,
    text: String,
}

fn number(s: &str) -> Option<u64> {
    // No leading zeros, as the specification requires.
    if s.is_empty() || (s.len() > 1 && s.starts_with('0')) || !s.bytes().all(|b| b.is_ascii_digit())
    {
        return None;
    }
    s.parse().ok()
}

impl Version {
    /// `X.Y.Z[-pre][+build]`; build metadata is ignored for ordering.
    pub fn parse(s: &str) -> Option<Self> {
        let main = s
            .split_once('+')
            .map_or(s, |(v, build)| if build.is_empty() { "" } else { v });
        let (core, pre) = match main.split_once('-') {
            Some((c, p)) => (c, Some(p)),
            None => (main, None),
        };
        let parts: Vec<&str> = core.split('.').collect();
        if parts.len() != 3 {
            return None;
        }
        let mut numbers = [0; 3];
        for (slot, part) in numbers.iter_mut().zip(&parts) {
            *slot = number(part)?;
        }
        let mut idents = Vec::new();
        if let Some(pre) = pre {
            for id in pre.split('.') {
                if id.is_empty() || !id.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-') {
                    return None;
                }
                idents.push(if id.bytes().all(|b| b.is_ascii_digit()) {
                    Ident::Num(number(id)?)
                } else {
                    Ident::Text(id.to_string())
                });
            }
        }
        Some(Self {
            core: numbers,
            pre: idents,
            text: main.to_string(),
        })
    }
    /// A release tag: `v` followed by a version.
    pub fn from_tag(tag: &str) -> Option<Self> {
        Self::parse(tag.strip_prefix('v')?)
    }
}

impl std::fmt::Display for Version {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        f.write_str(&self.text)
    }
}

impl Ord for Version {
    fn cmp(&self, other: &Self) -> Ordering {
        self.core
            .cmp(&other.core)
            .then_with(|| match (self.pre.is_empty(), other.pre.is_empty()) {
                (true, true) => Ordering::Equal,
                // A pre-release comes before its release.
                (true, false) => Ordering::Greater,
                (false, true) => Ordering::Less,
                (false, false) => {
                    for (a, b) in self.pre.iter().zip(&other.pre) {
                        let o = match (a, b) {
                            (Ident::Num(x), Ident::Num(y)) => x.cmp(y),
                            (Ident::Num(_), Ident::Text(_)) => Ordering::Less,
                            (Ident::Text(_), Ident::Num(_)) => Ordering::Greater,
                            (Ident::Text(x), Ident::Text(y)) => x.cmp(y),
                        };
                        if o != Ordering::Equal {
                            return o;
                        }
                    }
                    self.pre.len().cmp(&other.pre.len())
                }
            })
    }
}

impl PartialOrd for Version {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[cfg(test)]
mod tests {
    use super::Version;
    fn v(s: &str) -> Version {
        Version::parse(s).unwrap()
    }
    #[test]
    fn orders_like_the_specification() {
        let list = [
            "0.2.5",
            "0.2.6-alpha",
            "0.2.6-alpha.1",
            "0.2.6-alpha.beta",
            "0.2.6-beta",
            "0.2.6-beta.2",
            "0.2.6-beta.11",
            "0.2.6-rc.1",
            "0.2.6",
            "0.2.10",
            "0.10.0",
            "1.0.0",
        ];
        for pair in list.windows(2) {
            assert!(v(pair[0]) < v(pair[1]), "{} < {}", pair[0], pair[1]);
        }
        assert_eq!(v("0.2.6+build.7"), v("0.2.6"));
    }
    #[test]
    fn tags_need_the_v_prefix() {
        assert_eq!(Version::from_tag("v0.2.6").unwrap(), v("0.2.6"));
        for bad in [
            "0.2.6",
            "v0.2",
            "v0.2.06",
            "v1.2.3-",
            "v1.2.3-a..b",
            "beta-1-rc21-native",
            "v1.2.3+",
        ] {
            assert!(Version::from_tag(bad).is_none(), "{bad}");
        }
    }
}
