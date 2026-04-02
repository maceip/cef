//! Layer-3 agent API over [`cef`] (CEF Rust bindings). Implementations are
//! stubs until the async bridge to the CEF UI thread is wired.

mod browser;
mod element_refs;
mod error;
mod stealth;

pub use browser::{AgentBrowser, SnapshotSettings};
pub use element_refs::ElementRefIndex;
pub use error::AgentError;
pub use stealth::StealthConfig;

pub type Result<T> = std::result::Result<T, AgentError>;
