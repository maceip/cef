/// Placeholder for future stealth / fingerprint-hardening toggles.
#[derive(Debug, Clone, Default)]
pub struct StealthConfig {
    pub enabled: bool,
}

impl StealthConfig {
    pub fn new() -> Self {
        Self::default()
    }
}
