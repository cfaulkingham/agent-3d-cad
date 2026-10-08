fn main() {
    println!("cargo:rerun-if-changed=../VERSION");
    let version = std::fs::read_to_string("../VERSION").expect("VERSION");
    assert_eq!(version.trim(), env!("CARGO_PKG_VERSION"), "Cargo version must match VERSION");
    let config: String = std::fs::read_to_string("tauri.conf.json").expect("Tauri config");
    assert!(config.contains(&format!("\"version\": \"{}\"", version.trim())), "Tauri config version must match VERSION");
    let mut html = std::fs::read_to_string("../web/viewer.html").expect("Viewer HTML").replace("\r\n", "\n");
    println!("cargo:rerun-if-changed=../web/viewer.html");
    // The MCP resource prohibits network connections. Tauri's own CSP adds only
    // its private IPC transport, and hashes the same reviewed inline assets.
    let start = html.find("<meta http-equiv=\"Content-Security-Policy\"").expect("Viewer CSP");
    let end = start + html[start..].find('>').expect("CSP end") + 1;
    html.replace_range(start..end, "");
    for (name, ext) in [("styles", "css"), ("bridge", "js"), ("renderer", "js"), ("state", "js"), ("shell", "js"), ("app", "js")] {
        let path = format!("../web/{name}.{ext}");
        println!("cargo:rerun-if-changed={path}");
        html = html.replace(&format!("@VIEWER_{}@", name.to_uppercase()), &std::fs::read_to_string(path).expect("Viewer asset").replace("\r\n", "\n"));
    }
    std::fs::create_dir_all("ui").expect("UI directory");
    std::fs::write("ui/index.html", html).expect("UI HTML");
    tauri_build::build()
}
