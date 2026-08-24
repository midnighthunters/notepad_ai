// Copyright (C)2025 Don HO <don.h@free.fr>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#pragma once

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <string_view>

class ScintillaEditView;

namespace NppAi
{

// Runs a complete, user-approved AI edit against the current UTF-8 document or selection.
// The selected provider API key is requested only when absent and is persisted through Windows
// Credential Manager. The network request runs off the UI thread with a cancellable modal wait,
// so a slow provider never freezes Notepad++.
void RunNotepadAiEdit(HWND parent, HINSTANCE instance, ScintillaEditView & view);

// True when an API key is already stored for the currently selected provider.
[[nodiscard]] bool HasStoredApiKey();

// Stores (or, when apiKeyUtf8 is empty, erases) the current provider API key in Windows Credential
// Manager. Returns true on success. On failure, errorMessage receives a human-readable reason.
[[nodiscard]] bool StoreApiKey(std::string_view apiKeyUtf8, std::wstring & errorMessage);

// Sends a tiny live request to the currently configured provider to validate the stored key and
// connectivity. Runs off the UI thread with a cancellable modal wait. Returns true on success and
// fills message with the provider reply (success) or a diagnostic error (failure).
[[nodiscard]] bool TestConnection(HWND parent, HINSTANCE instance, std::wstring & message);

} // namespace NppAi
