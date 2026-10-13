# ScreenPilot project instructions

- Develop Windows first with C++ and Qt 6. Linux work starts after the Windows version is complete.
- Preserve the full fixed Mac reference scope recorded in the requirements and parity checklist.
- User-approved scope exception: do not implement Mac project-file compatibility, import/export conversion, or cross-app roundtrips. Maintain ScreenPilot's own project format; continue aligning product features, UI, editing behavior, algorithms, and output appearance.
- User-approved scope exception: do not implement iPhone/iPad/iOS device input or device audio capture. Keep screen, window, region, camera, microphone and Windows system/application audio input.
- Prioritize a usable application; keep verification focused on necessary build and user-flow checks.
- All native subagents must use `gpt-6.1-sol`.
- Before every public commit or push, publish only appropriate content. Exclude credentials, personal information, real local paths, device identifiers, detailed machine metadata, raw recordings, and raw validation/event logs. Keep private evidence locally in ignored directories; publish sanitized summaries and generic reproduction steps. Do not ask again about excluding unsuitable material.
- Inspect the entire outgoing commit history, not only the final working tree. Remove private material from unpublished commits before pushing; never silently rewrite already published history.
