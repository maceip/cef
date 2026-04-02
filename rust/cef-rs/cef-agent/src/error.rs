use thiserror::Error;

#[derive(Debug, Error)]
pub enum AgentError {
    #[error("not implemented: {0}")]
    Unimplemented(&'static str),
}
