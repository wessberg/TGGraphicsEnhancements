# Neural Rendering implementation

Native entry: native/neural/src/extension.cpp. Community adapter: community_adapter.cpp.
Managed integration: src/TGNeuralRendering. Shared guide/publication and NGX device ownership live in the bridge.

Signed model dispatch uses the SDK path. Modified/unsigned compatible runtimes select TGNeuralRendering.nvngx.dll
automatically. The helper owns its public NGX parameters/handles while sharing device/lock; it isolates the
caller-name requirement. No driver/model/Windows trust modification is applied. Other NGX consumers retain
their trust policy. Acceptance checks exports, x64/API and runtime support; hashes are diagnostics, not allowlists.

310.9.16 and310.8 are known signed profiles. Current model requests lightweight preset B, independently of
appearance selection. No control mask is supplied. Check the model's geometry guides, exposure and colour domain alongside evaluation success and image quality.

Unity 6000.3 reflected CustomPass invocation is handled in Shared/CustomPassRecorder.cs for both branches.
Retained native child-row arrays prevent the normal-version Options lifetime crash. Managed/native build,
runtime creation, evaluation, menu stability, HDR output, appearance and hardware support are separate checks.

The complete implementation chronology and hashes are archived under this same document path.
Current validation limits: [DLSS5-VALIDATION.md](DLSS5-VALIDATION.md).
