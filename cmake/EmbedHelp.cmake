# Only user-facing capability guides belong in help, not historical handoffs,
# release claims or development plans. Schemas are taken from native definitions.
set(help_documents NATIVE_HELP PROTOCOL PARAMETRIC_EXPRESSIONS RICHER_MODELING
  SHELL_OFFSET_THICKEN SKETCH_OPERATIONS AUTHORING_IMPORTS SHEET_METAL SURFACES
  PARITY_CURVES PARITY_SOLIDS PARITY_AUTHORING PARITY_SHEET_METAL PARITY_SURFACES
  COMPONENTS ASSEMBLIES MEASUREMENTS SECTIONS DRAWINGS COMMIT_RECEIPTS
  ARTIFACT_REVIEW MESH_RECONSTRUCTION RENDERING APPEARANCE PRESENTATION
  PLAYBACK MARKUP ANNOTATIONS PROGRESSIVE_VIEWER EXPORT_DOWNLOADS
  MANUFACTURING FABRICATION_REVIEW PURCHASED_PARTS SLICING GCODE_REVIEW
  PRINTER_HANDOFF ROBOT_EXPORT)
set(help_cpp "#include \"agentcad/help.hpp\"\nnamespace agentcad {\nconst std::vector<HelpGuide>& help_guides() {\n")
set(help_entries "")
foreach(name IN LISTS help_documents)
  set(help_path "${CMAKE_CURRENT_SOURCE_DIR}/docs/${name}.md")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${help_path}")
  file(READ "${help_path}" help_text)
  string(REPLACE "\r\n" "\n" help_text "${help_text}")
  string(HEX "${help_text}" help_hex)
  string(REGEX REPLACE "(..)" "0x\\1,\n" help_bytes "${help_hex}")
  # Bytes preserve UTF-8 and avoid MSVC's string-literal length limit.
  string(APPEND help_cpp "static constexpr unsigned char ${name}[] = {${help_bytes}};\n")
  string(APPEND help_entries "{\"docs/${name}.md\", std::string(reinterpret_cast<const char*>(${name}), sizeof(${name}))},\n")
endforeach()
string(APPEND help_cpp "static const std::vector<HelpGuide> guides = {${help_entries}};\nreturn guides;\n}\n}\n")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/help_guides.cpp" CONTENT "${help_cpp}" @ONLY)
