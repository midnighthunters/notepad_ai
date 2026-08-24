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

#include "AiProvider.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace NppAi
{

struct AiValidatedEndpoint
{
	bool usesHttps = true;
	std::string host;
	std::uint16_t port = 443;
	std::string target;
};

// HTTPS is accepted for valid authority components. Plain HTTP is accepted only for localhost,
// 127.0.0.1, or [::1]; redirects are disabled by WinHttpAiTransport.
[[nodiscard]] AiResult<AiValidatedEndpoint> AiValidateEndpointUrl(std::string_view url);

struct AiWinHttpTransportOptions
{
	std::size_t maxRequestBytes = 1024 * 1024;
	std::size_t maxResponseBytes = 1024 * 1024;
	std::uint32_t resolveTimeoutMilliseconds = 10 * 1000;
	std::uint32_t connectTimeoutMilliseconds = 15 * 1000;
	std::uint32_t sendTimeoutMilliseconds = 30 * 1000;
	// Generative endpoints (e.g. Gemini "thinking" models) can legitimately take well over 30s to
	// produce a first byte. A short receive timeout surfaced as "WinHTTP request failed". The request
	// runs off the UI thread and is user-cancellable, so a generous bound is safe here.
	std::uint32_t receiveTimeoutMilliseconds = 120 * 1000;
};

// Uses WinHTTP with Windows automatic proxy discovery, certificate validation left enabled, redirect
// following disabled, bounded bodies, and a stop-token callback that closes the request handle.
class WinHttpAiTransport final : public IAiTransport
{
public:
	explicit WinHttpAiTransport(AiWinHttpTransportOptions options = {});
	[[nodiscard]] AiResult<AiHttpResponse> send(const AiHttpRequest & request, std::stop_token stopToken) override;

private:
	AiWinHttpTransportOptions _options;
};

} // namespace NppAi
