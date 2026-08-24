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

class ScintillaEditView;

namespace NppAi
{

// Runs a complete, user-approved Gemini edit against the current UTF-8 document or selection.
// The API key is requested only when absent and is persisted through Windows Credential Manager.
void RunNotepadAiEdit(HWND parent, HINSTANCE instance, ScintillaEditView & view);

} // namespace NppAi
