use serde_json::{json, Value};
use std::{io::{BufRead, BufReader, Read, Write}, path::Path, process::{Child, ChildStdin, Command, Stdio}, sync::mpsc, time::Duration};

pub struct Client {
    child: Child,
    input: ChildStdin,
    replies: mpsc::Receiver<Result<Value, String>>,
    next: u64,
}
impl Client {
    pub fn start(exe: &Path, workspace: &Path) -> Result<Self, String> {
        let mut command = Command::new(exe);
        command.args(["serve", "--workspace"]).arg(workspace).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::inherit());
        #[cfg(windows)] {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000); // CREATE_NO_WINDOW
        }
        let mut child = command.spawn().map_err(|e| format!("Cannot start native CAD service: {e}"))?;
        let input = child.stdin.take().ok_or("Missing service stdin")?;
        let stdout = child.stdout.take().ok_or("Missing service stdout")?;
        let (sender, replies) = mpsc::sync_channel(32);
        std::thread::spawn(move || {
            let mut reader = BufReader::new(stdout);
            loop {
                let mut line = Vec::new();
                match reader.by_ref().take(2 * 1024 * 1024 + 1).read_until(b'\n', &mut line) {
                    Ok(0) => break,
                    Ok(n) if n > 2 * 1024 * 1024 => { let _ = sender.send(Err("Native response exceeds 2 MiB".into())); break; }
                    Ok(_) => {
                        if line.iter().all(u8::is_ascii_whitespace) { continue; }
                        if sender.send(serde_json::from_slice(&line).map_err(|e| e.to_string())).is_err() { break; }
                    }
                    Err(e) => { let _ = sender.send(Err(e.to_string())); break; }
                }
            }
        });
        let mut result = Self { child, input, replies, next: 1 };
        result.request("initialize", json!({"protocolVersion":"2025-11-25", "capabilities":{}, "clientInfo":{"name":"agent-cad-viewer", "version":env!("CARGO_PKG_VERSION")}}))?;
        writeln!(result.input, "{{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}}").map_err(|e| e.to_string())?;
        Ok(result)
    }
    fn request(&mut self, method: &str, params: Value) -> Result<Value, String> {
        let id = self.next; self.next += 1;
        let request = json!({"jsonrpc":"2.0", "id":id, "method":method, "params":params}).to_string();
        if request.len() > 1024 * 1024 { return Err("Request exceeds 1 MiB".into()); }
        writeln!(self.input, "{request}").map_err(|e| e.to_string())?;
        let response = match self.replies.recv_timeout(Duration::from_secs(20)) {
            Ok(reply) => reply?,
            Err(e) => { let _ = self.child.kill(); return Err(format!("CAD service did not respond: {e}")); }
        };
        if response["id"] != id { let _ = self.child.kill(); return Err("Unexpected native response identity".into()); }
        if !response["error"].is_null() { return Err(response["error"]["message"].as_str().unwrap_or("Native protocol failure").into()); }
        Ok(response["result"].clone())
    }
    pub fn call(&mut self, name: &str, args: Value) -> Result<Value, String> {
        self.request("tools/call", json!({"name":name, "arguments":args}))
    }
    pub fn value(&mut self, name: &str, args: Value) -> Result<Value, String> {
        let retryable = ["cad_list", "cad_context", "cad_open"].contains(&name) ||
            (name == "cad_job" && args["action"] == "get");
        let deadline = std::time::Instant::now() + Duration::from_secs(5);
        let result = loop {
            let result = self.call(name, args.clone())?;
            if retryable && result["isError"] == true && result["structuredContent"]["error"]["code"] == "workspace_busy" && std::time::Instant::now() < deadline {
                std::thread::sleep(Duration::from_millis(50));
                continue;
            }
            break result;
        };
        if result["isError"] == true { return Err(result["structuredContent"]["error"]["message"].as_str().unwrap_or("CAD operation failed").into()); }
        Ok(result["structuredContent"].clone())
    }
}
impl Drop for Client {
    fn drop(&mut self) { let _ = self.child.kill(); let _ = self.child.wait(); }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn desktop_and_mcp_use_the_same_viewer() {
        let exe = std::env::var_os("CAD_SERVICE_EXE").expect("Set CAD_SERVICE_EXE");
        let root = std::env::temp_dir().join(format!("cad-viewer-parity-{}", uuid::Uuid::new_v4()));
        let mut client = Client::start(Path::new(&exe), &root).unwrap();
        let resource = client.request("resources/read", json!({"uri":"ui://agent-3d-cad/viewer.html"})).unwrap();
        let html = resource["contents"][0]["text"].as_str().unwrap();
        // Only the host security policy differs; Tauri supplies its IPC CSP.
        let start = html.find("<meta http-equiv=\"Content-Security-Policy\"").unwrap();
        let end = start + html[start..].find('>').unwrap() + 1;
        let desktop_html = format!("{}{}", &html[..start], &html[end..]);
        assert_eq!(include_str!("../ui/index.html"), desktop_html,
                   "Rebuild both the native service and desktop from the same viewer sources");
        drop(client);
        std::fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn reopens_projects_and_exports_through_native_service() {
        let exe = std::env::var_os("CAD_SERVICE_EXE").expect("Set CAD_SERVICE_EXE for native integration tests");
        let root = std::env::temp_dir().join(format!("cad-tauri-{}", uuid::Uuid::new_v4()));
        {
            let mut client = Client::start(Path::new(&exe), &root).unwrap();
            client.value("cad_create", json!({"document_id":"older_project", "model":{"schema_version":1,"units":"mm","parameters":{},"features":[{"id":"box","type":"box","size":[20,10,5]}],"output":"box"}})).unwrap();
        }
        {
            let mut client = Client::start(Path::new(&exe), &root).unwrap();
            assert_eq!(client.value("cad_list", json!({})).unwrap()["documents"][0]["document_id"], "older_project");
            let mut job = client.value("cad_job", json!({"action":"submit","request_id":"desktop_export","tool":"cad_export","arguments":{"document_id":"older_project","revision":1,"format":"step"}})).unwrap();
            for _ in 0..400 {
                if job["state"] == "succeeded" { break; }
                assert!(job["state"] == "queued" || job["state"] == "running", "{job}");
                std::thread::sleep(Duration::from_millis(50));
                job = client.value("cad_job", json!({"action":"get", "job_id":"desktop_export"})).unwrap();
            }
            assert_eq!(job["state"], "succeeded");
            assert!(Path::new(job["result"]["path"].as_str().unwrap()).is_file());
            assert!(client.value("cad_show", json!({"document_id":"missing"})).is_err());
        }
        std::fs::remove_dir_all(root).unwrap();
    }
}
