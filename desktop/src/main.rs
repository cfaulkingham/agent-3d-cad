#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod client;
use client::Client;
use serde::Deserialize;
use serde_json::{json, Value};
use std::{fs, path::PathBuf, sync::{Arc, Mutex, atomic::{AtomicBool, Ordering}}, time::{Duration, Instant}};
use tauri::{Manager, WebviewWindow};
use tauri_plugin_dialog::DialogExt;

struct Session { client: Client, workspace: PathBuf, launch: Value }
struct Desktop {
    session: Mutex<Option<Session>>, busy: AtomicBool, exe: PathBuf, initial: PathBuf, view: String, preferences: PathBuf,
}
type Shared = Arc<Desktop>;
type Result<T> = std::result::Result<T, String>;
struct Busy(Shared);
impl Busy {
    fn enter(state: &Shared) -> Result<Self> {
        state.busy.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).map_err(|_| "Wait for the workspace operation to finish")?;
        Ok(Self(state.clone()))
    }
}
impl Drop for Busy { fn drop(&mut self) { self.0.busy.store(false, Ordering::Release); } }
fn local_url(url: &tauri::Url) -> bool {
    (url.scheme() == "tauri" && url.host_str() == Some("localhost")) ||
    (["http", "https"].contains(&url.scheme()) && url.host_str() == Some("tauri.localhost"))
}
fn check(window: &WebviewWindow) -> Result<()> {
    if window.label() != "main" || !local_url(&window.url().map_err(|e| e.to_string())?) { return Err("Unknown viewer sender".into()); }
    Ok(())
}
fn recent(state: &Desktop) -> Vec<PathBuf> {
    fs::read(&state.preferences).ok().and_then(|bytes| serde_json::from_slice::<Vec<PathBuf>>(&bytes).ok()).unwrap_or_default().into_iter().take(12).collect()
}
fn open(state: &Desktop, path: PathBuf) -> Result<Value> {
    let mut client = Client::start(&state.exe, &path)?;
    let mut launch = client.value("cad_open", json!({"view_id":state.view}))?;
    let path = fs::canonicalize(path).map_err(|e| e.to_string())?;
    launch["workspace"] = json!(path);
    let mut history = recent(state); history.retain(|folder| folder != &path); history.insert(0, path.clone()); history.truncate(12);
    fs::create_dir_all(state.preferences.parent().ok_or("No settings folder")?).map_err(|e| e.to_string())?;
    fs::write(&state.preferences, serde_json::to_vec(&history).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    *state.session.lock().map_err(|e| e.to_string())? = Some(Session { client, workspace: path, launch: launch.clone() });
    Ok(launch)
}
fn tool(state: &Desktop, name: &str, args: Value) -> Result<Value> {
    state.session.lock().map_err(|e| e.to_string())?.as_mut().ok_or("Open a workspace first")?.client.value(name, args)
}
async fn blocking<T: Send + 'static>(f: impl FnOnce() -> Result<T> + Send + 'static) -> Result<T> {
    tauri::async_runtime::spawn_blocking(f).await.map_err(|e| e.to_string())?
}
#[tauri::command]
async fn initialize(window: WebviewWindow, state: tauri::State<'_, Shared>) -> Result<Value> {
    check(&window)?; let state = state.inner().clone();
    blocking(move || {
        let _busy = Busy::enter(&state)?;
        if let Some(session) = state.session.lock().map_err(|e| e.to_string())?.as_ref() { return Ok(session.launch.clone()); }
        open(&state, state.initial.clone())
    }).await
}
#[tauri::command]
async fn call_tool(window: WebviewWindow, state: tauri::State<'_, Shared>, name: String, args: Value) -> Result<Value> {
    check(&window)?;
    if !["cad_list", "cad_show", "cad_context", "cad_viewer"].contains(&name.as_str()) { return Err("This tool is not available in the viewer".into()); }
    if name != "cad_list" && args["view_id"] != state.view { return Err("View does not belong to this window".into()); }
    let state = state.inner().clone();
    blocking(move || state.session.lock().map_err(|e| e.to_string())?.as_mut().ok_or("Open a workspace first")?.client.call(&name, args)).await
}
#[tauri::command]
fn recent_workspaces(window: WebviewWindow, state: tauri::State<'_, Shared>) -> Result<Vec<PathBuf>> { check(&window)?; Ok(recent(&state)) }
#[tauri::command]
async fn open_workspace(window: WebviewWindow, state: tauri::State<'_, Shared>) -> Result<()> {
    check(&window)?; let state = state.inner().clone();
    blocking(move || {
        let _busy = Busy::enter(&state)?;
        if let Some(folder) = window.app_handle().dialog().file().set_parent(&window).set_title("Open a CAD workspace").blocking_pick_folder() {
            open(&state, folder.into_path().map_err(|e| e.to_string())?)?;
            window.eval("location.reload()").map_err(|e| e.to_string())?;
        }
        Ok(())
    }).await
}
#[tauri::command]
async fn open_recent(window: WebviewWindow, state: tauri::State<'_, Shared>, index: usize) -> Result<()> {
    check(&window)?; let state = state.inner().clone();
    blocking(move || {
        let _busy = Busy::enter(&state)?;
        let folder = recent(&state).get(index).cloned().ok_or("Recent workspace no longer exists")?;
        if !folder.is_dir() { return Err("Workspace folder is missing. Use Open workspace to locate it.".into()); }
        open(&state, folder)?; window.eval("location.reload()").map_err(|e| e.to_string())
    }).await
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct ExportRequest { document_id: String, revision: u64, format: String }
fn generate_export(state: &Desktop, request: &ExportRequest) -> Result<Vec<PathBuf>> {
    if !["stl", "step", "pdf", "svg", "dxf"].contains(&request.format.as_str()) || request.revision == 0 { return Err("Choose a saved model revision and export format".into()); }
    let drawing = ["pdf", "svg", "dxf"].contains(&request.format.as_str());
    let mut args = json!({"document_id":request.document_id, "revision":request.revision});
    if drawing { args["drawing"] = json!({"formats":[request.format]}); } else { args["format"] = json!(request.format); }
    let id = format!("export_{}", uuid::Uuid::new_v4());
    let mut job = tool(state, "cad_job", json!({"action":"submit","request_id":id,"tool":if drawing {"cad_drawing"} else {"cad_export"},"arguments":args,"budget":{"timeout_ms":300000,"memory_mb":2048}}))?;
    let deadline = Instant::now() + Duration::from_secs(310);
    while ["queued", "running", "cancelling"].contains(&job["state"].as_str().unwrap_or("")) && Instant::now() < deadline {
        std::thread::sleep(Duration::from_millis(250));
        job = tool(state, "cad_job", json!({"action":"get","job_id":id}))?;
    }
    if job["state"] != "succeeded" { return Err(format!("Export job {id}: {}", job["error"]["message"].as_str().unwrap_or("did not complete"))); }
    let files: Vec<PathBuf> = if drawing {
        job["result"]["artifacts"].as_array().ok_or("Missing export artifacts")?.iter().filter(|file| file["format"] == request.format)
            .map(|file| file["path"].as_str().map(PathBuf::from).ok_or_else(|| "Missing artifact path".to_string())).collect::<Result<_>>()?
    } else { vec![PathBuf::from(job["result"]["path"].as_str().ok_or("Missing export path")?)] };
    let root = state.session.lock().map_err(|e| e.to_string())?.as_ref().ok_or("Missing workspace")?.workspace.join("exports").canonicalize().map_err(|e| e.to_string())?;
    if files.is_empty() { return Err("Export produced no files".into()); }
    files.into_iter().map(|file| {
        let real = file.canonicalize().map_err(|e| e.to_string())?;
        if !real.starts_with(&root) || !real.is_file() { return Err("Export path escapes the workspace".into()); } Ok(real)
    }).collect()
}
#[tauri::command]
async fn export_model(window: WebviewWindow, state: tauri::State<'_, Shared>, request: ExportRequest) -> Result<Value> {
    check(&window)?; let state = state.inner().clone();
    blocking(move || {
        let _busy = Busy::enter(&state)?;
        let files = generate_export(&state, &request)?;
        let dialog = window.app_handle().dialog().file().set_parent(&window);
        if files.len() == 1 {
            let filename = format!("{}-r{}.{}", request.document_id, request.revision, request.format);
            if let Some(target) = dialog.set_title("Save export").set_file_name(filename).add_filter(request.format.to_uppercase(), &[&request.format]).blocking_save_file() {
                let path = target.into_path().map_err(|e| e.to_string())?;
                fs::copy(&files[0], &path).map_err(|e| e.to_string())?; return Ok(json!({"paths":[path]}));
            }
        } else if let Some(target) = dialog.set_title("Choose a folder for the DXF views").blocking_pick_folder() {
            let folder = target.into_path().map_err(|e| e.to_string())?.join(format!("{}-r{}-{}", request.document_id, request.revision, uuid::Uuid::new_v4()));
            fs::create_dir(&folder).map_err(|e| e.to_string())?;
            let mut saved = Vec::new();
            for file in files { let target = folder.join(file.file_name().ok_or("Missing file name")?); fs::copy(file, &target).map_err(|e| e.to_string())?; saved.push(target); }
            return Ok(json!({"paths":saved}));
        }
        Ok(json!({"cancelled":true}))
    }).await
}
fn option(name: &str) -> Option<String> { let args: Vec<_> = std::env::args().collect(); args.windows(2).find(|a| a[0] == name).map(|a| a[1].clone()) }
fn executable() -> Result<PathBuf> {
    #[cfg(debug_assertions)]
    if let Some(path) = std::env::var_os("CAD_SERVICE_EXE") { return Ok(path.into()); }
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    let root = exe.ancestors().nth(if cfg!(target_os = "macos") {5} else {2}).ok_or("Cannot locate native bundle")?;
    Ok(root.join(if cfg!(windows) {"bin/agent-3d-cad.exe"} else {"bin/agent-3d-cad"}))
}
fn main() {
    tauri::Builder::default().plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![initialize, call_tool, open_workspace, recent_workspaces, open_recent, export_model])
        .setup(|app| {
            let preferences = app.path().app_config_dir()?.join("workspaces.json");
            #[cfg(debug_assertions)]
            let preferences = std::env::var_os("CAD_VIEWER_CONFIG_DIR").map(|directory| PathBuf::from(directory).join("workspaces.json")).unwrap_or(preferences);
            let history: Vec<PathBuf> = fs::read(&preferences).ok().and_then(|data| serde_json::from_slice(&data).ok()).unwrap_or_default();
            let initial = option("--workspace").map(PathBuf::from).or_else(|| history.first().filter(|p| p.is_dir()).cloned()).unwrap_or(app.path().document_dir()?.join("Agent CAD"));
            app.manage(Arc::new(Desktop { session: Mutex::new(None), busy: AtomicBool::new(false), exe: executable().map_err(std::io::Error::other)?, initial, view: option("--view").unwrap_or("main".into()), preferences }));
            tauri::WebviewWindowBuilder::new(app, "main", tauri::WebviewUrl::default()).title("Agent CAD").inner_size(1440.0, 900.0).min_inner_size(960.0, 640.0).on_navigation(local_url).build()?;
            Ok(())
        }).run(tauri::generate_context!()).expect("Unable to run Agent CAD");
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn desktop_exports_all_formats_without_changing_saved_source() {
        let root = std::env::temp_dir().join(format!("cad-desktop-{}", uuid::Uuid::new_v4()));
        let state = Desktop { session: Mutex::new(None), busy: AtomicBool::new(false),
            exe: std::env::var_os("CAD_SERVICE_EXE").expect("Set CAD_SERVICE_EXE").into(),
            initial: root.join("workspace"), view: "main".into(), preferences: root.join("settings/workspaces.json") };
        open(&state, state.initial.clone()).unwrap();
        tool(&state, "cad_create", json!({"document_id":"part","model":{"schema_version":1,"units":"mm","parameters":{},"features":[{"id":"box","type":"box","size":[20,10,5]}],"output":"box"}})).unwrap();
        let original = tool(&state, "cad_read", json!({"document_id":"part"})).unwrap();
        for format in ["step", "stl", "pdf", "svg", "dxf"] {
            let files = generate_export(&state, &ExportRequest { document_id: "part".into(), revision: 1, format: format.into() }).unwrap();
            assert_eq!(files.len(), if format == "dxf" {4} else {1}, "{format}");
            for path in files {
                assert_eq!(path.extension().unwrap(), format);
                let bytes = fs::read(path).unwrap();
                assert!(bytes.len() > 100, "Empty {format} output");
                if format == "pdf" { assert!(bytes.starts_with(b"%PDF-")); }
                if format == "step" { assert!(bytes.starts_with(b"ISO-10303-21;")); }
            }
        }
        assert_eq!(tool(&state, "cad_read", json!({"document_id":"part"})).unwrap(), original);
        assert_eq!(recent(&state), vec![state.initial.canonicalize().unwrap()]);
        assert!(generate_export(&state, &ExportRequest { document_id: "part".into(), revision: 1, format: "exe".into() }).is_err());
        drop(state);
        fs::remove_dir_all(root).unwrap();
    }
}
