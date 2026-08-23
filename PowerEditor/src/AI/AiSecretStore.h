// This file is part of Notepad++ project
// Copyright (C)2025 Don HO <don.h@free.fr>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "AiCore.h"

#include <string>
#include <string_view>

namespace NppAi
{

// Implementations must persist credentials in an OS-protected store. There is intentionally no
// plaintext file, registry, or environment-variable fallback in this core.
class IAiSecretStore
{
public:
	virtual ~IAiSecretStore() = default;
	[[nodiscard]] virtual AiStatus save(std::wstring_view targetName, std::string_view secret) = 0;
	[[nodiscard]] virtual AiResult<std::string> load(std::wstring_view targetName) = 0;
	[[nodiscard]] virtual AiStatus erase(std::wstring_view targetName) = 0;
};

class WindowsCredentialManagerSecretStore final : public IAiSecretStore
{
public:
	[[nodiscard]] AiStatus save(std::wstring_view targetName, std::string_view secret) override;
	[[nodiscard]] AiResult<std::string> load(std::wstring_view targetName) override;
	[[nodiscard]] AiStatus erase(std::wstring_view targetName) override;
};

} // namespace NppAi
