//! [`AgentBrowser`] composes CEF primitives for agent workflows.

use crate::{element_refs::ElementRefIndex, error::AgentError, stealth::StealthConfig, Result};
use cef::{Browser, BrowserCapture, StorageStateManager};
use serde_json::Value;

/// Settings forwarded to `BrowserCapture::snapshot` / `eval_then_snapshot`.
#[derive(Debug, Clone, Default)]
pub struct SnapshotSettings {
    pub interactive_only: bool,
    pub compact: bool,
    pub max_depth: i32,
    pub selector: Option<String>,
}

/// Browser handle for automation: main browser plus capture and storage state APIs.
#[derive(Clone)]
pub struct AgentBrowser {
    pub browser: Browser,
    pub capture: BrowserCapture,
    pub storage: StorageStateManager,
    refs: ElementRefIndex,
    stealth: StealthConfig,
}

impl AgentBrowser {
    pub fn new(browser: Browser, capture: BrowserCapture, storage: StorageStateManager) -> Self {
        Self {
            browser,
            capture,
            storage,
            refs: ElementRefIndex::default(),
            stealth: StealthConfig::default(),
        }
    }

    pub async fn navigate(&self, _url: &str) -> Result<()> {
        Err(AgentError::Unimplemented("navigate"))
    }

    pub async fn snapshot(&self, _settings: SnapshotSettings) -> Result<String> {
        Err(AgentError::Unimplemented("snapshot"))
    }

    pub async fn eval(&self, _js: &str) -> Result<Value> {
        Err(AgentError::Unimplemented("eval"))
    }

    pub async fn click(&self, _selector: &str) -> Result<()> {
        Err(AgentError::Unimplemented("click"))
    }

    pub async fn fill(&self, _selector: &str, _value: &str) -> Result<()> {
        Err(AgentError::Unimplemented("fill"))
    }

    pub async fn screenshot(&self, _path: &str, _annotate: bool) -> Result<String> {
        Err(AgentError::Unimplemented("screenshot"))
    }

    pub fn stealth(&self) -> StealthConfig {
        self.stealth.clone()
    }

    pub fn refs(&self) -> &ElementRefIndex {
        &self.refs
    }
}
