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

#include "AiWinHttpTransport.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

namespace NppAi
{
namespace
{

[[nodiscard]] bool hasControlCharacter(std::string_view value) noexcept
{
	for (const unsigned char character : value)
	{
		if (character <= 0x1fU || character == 0x7fU)
			return true;
	}
	return false;
}

[[nodiscard]] std::string asciiLower(std::string_view value)
{
	std::string result;
	result.reserve(value.size());
	for (const unsigned char character : value)
	{
		if (character >= 'A' && character <= 'Z')
			result.push_back(static_cast<char>(character - 'A' + 'a'));
		else
			result.push_back(static_cast<char>(character));
	}
	return result;
}

[[nodiscard]] bool isDnsHostCharacter(char character) noexcept
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		(character >= '0' && character <= '9') || character == '-' || character == '.';
}

[[nodiscard]] bool isIpv6Character(char character) noexcept
{
	return (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F') ||
		(character >= '0' && character <= '9') || character == ':' || character == '.';
}

[[nodiscard]] bool isLoopbackHost(std::string_view host) noexcept
{
	return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

[[nodiscard]] AiResult<std::uint16_t> parsePort(std::string_view text)
{
	if (text.empty())
		return AiMakeError(AiErrorCode::TransportRejected, "Endpoint port is empty.");
	std::uint32_t port = 0;
	for (const char character : text)
	{
		if (character < '0' || character > '9')
			return AiMakeError(AiErrorCode::TransportRejected, "Endpoint port is not numeric.");
		port = port * 10U + static_cast<std::uint32_t>(character - '0');
		if (port > 65535U)
			return AiMakeError(AiErrorCode::TransportRejected, "Endpoint port is outside the valid range.");
	}
	if (port == 0)
		return AiMakeError(AiErrorCode::TransportRejected, "Endpoint port must be non-zero.");
	return static_cast<std::uint16_t>(port);
}

[[nodiscard]] AiStatus validateHeader(const AiHttpHeader & header)
{
	if (header.name.empty() || !AiIsValidUtf8(header.name) || !AiIsValidUtf8(header.value))
		return AiMakeError(AiErrorCode::TransportRejected, "HTTP header name or value is not valid UTF-8.");
	for (const char character : header.name)
	{
		const bool tokenCharacter = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
			(character >= '0' && character <= '9') || character == '-';
		if (!tokenCharacter)
			return AiMakeError(AiErrorCode::TransportRejected, "HTTP header name contains an unsafe character.");
	}
	if (header.value.find('\r') != std::string::npos || header.value.find('\n') != std::string::npos || header.value.find('\0') != std::string::npos)
		return AiMakeError(AiErrorCode::TransportRejected, "HTTP header value contains a line-break or null character.");
	return AiSuccess();
}

[[nodiscard]] AiError cancelledError()
{
	return AiMakeError(AiErrorCode::Cancelled, "HTTP request was cancelled.");
}

#ifdef _WIN32

class ScopedWinHttpHandle
{
public:
	explicit ScopedWinHttpHandle(HINTERNET handle = nullptr) : _handle(handle)
	{
	}

	~ScopedWinHttpHandle()
	{
		if (_handle != nullptr)
			::WinHttpCloseHandle(_handle);
	}

	ScopedWinHttpHandle(const ScopedWinHttpHandle &) = delete;
	ScopedWinHttpHandle & operator=(const ScopedWinHttpHandle &) = delete;

	[[nodiscard]] HINTERNET get() const noexcept
	{
		return _handle;
	}

private:
	HINTERNET _handle;
};

class CancellableRequestHandle
{
public:
	~CancellableRequestHandle()
	{
		cancel();
	}

	[[nodiscard]] bool attach(HINTERNET handle) noexcept
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_cancelled)
		{
			::WinHttpCloseHandle(handle);
			return false;
		}
		_handle = handle;
		return true;
	}

	void cancel() noexcept
	{
		HINTERNET handle = nullptr;
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_cancelled = true;
			handle = _handle;
			_handle = nullptr;
		}
		if (handle != nullptr)
			::WinHttpCloseHandle(handle);
	}

private:
	std::mutex _mutex;
	HINTERNET _handle = nullptr;
	bool _cancelled = false;
};

[[nodiscard]] AiResult<std::wstring> toWide(std::string_view utf8)
{
	if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || !AiIsValidUtf8(utf8))
		return AiMakeError(AiErrorCode::TransportRejected, "HTTP text is not convertible from strict UTF-8.");
	if (utf8.empty())
		return std::wstring {};
	const int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	if (required <= 0)
		return AiMakeError(AiErrorCode::TransportRejected, "HTTP text could not be converted to UTF-16.");
	std::wstring wide(static_cast<std::size_t>(required), L'\0');
	if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(), required) != required)
		return AiMakeError(AiErrorCode::TransportRejected, "HTTP text could not be converted to UTF-16.");
	return wide;
}

[[nodiscard]] AiError winHttpError(std::stop_token stopToken)
{
	if (stopToken.stop_requested())
		return cancelledError();
	return AiMakeError(AiErrorCode::TransportFailure, "WinHTTP request failed.");
}

#endif

} // namespace

AiResult<AiValidatedEndpoint> AiValidateEndpointUrl(std::string_view url)
{
	if (url.empty() || url.size() > 2048 || !AiIsValidUtf8(url) || hasControlCharacter(url) || url.find('\\') != std::string_view::npos)
		return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL contains an unsafe character or exceeds the limit.");
	const std::size_t schemeEnd = url.find("://");
	if (schemeEnd == std::string_view::npos)
		return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL must include an HTTP or HTTPS scheme.");
	const std::string scheme = asciiLower(url.substr(0, schemeEnd));
	if (scheme != "https" && scheme != "http")
		return AiMakeError(AiErrorCode::TransportRejected, "Only HTTPS endpoints, or explicitly loopback HTTP endpoints, are allowed.");

	const std::size_t authorityStart = schemeEnd + 3;
	if (authorityStart >= url.size())
		return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL has no authority.");
	const std::size_t targetStart = url.find_first_of("/?#", authorityStart);
	const std::string_view authority = url.substr(authorityStart, targetStart == std::string_view::npos ? std::string_view::npos : targetStart - authorityStart);
	if (authority.empty() || authority.find('@') != std::string_view::npos)
		return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL authority is empty or contains user-info.");
	if (targetStart != std::string_view::npos && url[targetStart] == '#')
		return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL fragments are not allowed.");
	if (url.find('#', targetStart == std::string_view::npos ? authorityStart : targetStart) != std::string_view::npos)
		return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL fragments are not allowed.");

	std::string host;
	std::string_view portText;
	if (authority.front() == '[')
	{
		const std::size_t closingBracket = authority.find(']');
		if (closingBracket == std::string_view::npos || closingBracket == 1)
			return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL has an invalid bracketed host.");
		const std::string_view rawHost = authority.substr(1, closingBracket - 1);
		if (!std::all_of(rawHost.begin(), rawHost.end(), isIpv6Character))
			return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL has an invalid IPv6 host.");
		host = asciiLower(rawHost);
		const std::string_view remainder = authority.substr(closingBracket + 1);
		if (!remainder.empty())
		{
			if (!remainder.starts_with(':'))
				return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL has an invalid host suffix.");
			portText = remainder.substr(1);
		}
	}
	else
	{
		const std::size_t colon = authority.find(':');
		if (colon != std::string_view::npos && authority.find(':', colon + 1) != std::string_view::npos)
			return AiMakeError(AiErrorCode::TransportRejected, "IPv6 endpoint hosts must use brackets.");
		const std::string_view rawHost = authority.substr(0, colon);
		if (rawHost.empty() || !std::all_of(rawHost.begin(), rawHost.end(), isDnsHostCharacter))
			return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL has an invalid host.");
		host = asciiLower(rawHost);
		if (colon != std::string_view::npos)
			portText = authority.substr(colon + 1);
	}

	AiValidatedEndpoint endpoint;
	endpoint.usesHttps = scheme == "https";
	endpoint.host = std::move(host);
	endpoint.port = endpoint.usesHttps ? 443 : 80;
	if (!portText.empty())
	{
		const AiResult<std::uint16_t> port = parsePort(portText);
		if (!port)
			return port.error();
		endpoint.port = port.value();
	}
	else if ((!authority.empty() && authority.back() == ':'))
	{
		return AiMakeError(AiErrorCode::TransportRejected, "Endpoint URL port is empty.");
	}
	if (!endpoint.usesHttps && !isLoopbackHost(endpoint.host))
		return AiMakeError(AiErrorCode::TransportRejected, "Plain HTTP is allowed only for an explicit loopback endpoint.");

	if (targetStart == std::string_view::npos)
		endpoint.target = "/";
	else
	{
		endpoint.target = std::string(url.substr(targetStart));
		if (endpoint.target.front() == '?')
			endpoint.target.insert(endpoint.target.begin(), '/');
	}
	return endpoint;
}

WinHttpAiTransport::WinHttpAiTransport(AiWinHttpTransportOptions options) : _options(std::move(options))
{
}

AiResult<AiHttpResponse> WinHttpAiTransport::send(const AiHttpRequest & request, std::stop_token stopToken)
{
	if (stopToken.stop_requested())
		return cancelledError();
	if (request.method != "POST" || !AiIsValidUtf8(request.body) || request.body.size() > _options.maxRequestBytes || _options.maxRequestBytes == 0 || _options.maxResponseBytes == 0)
		return AiMakeError(AiErrorCode::TransportRejected, "HTTP request method, UTF-8 body, or configured size limit is invalid.");
	for (const AiHttpHeader & header : request.headers)
	{
		const AiStatus headerStatus = validateHeader(header);
		if (!headerStatus)
			return headerStatus.error();
	}
	const AiResult<AiValidatedEndpoint> endpoint = AiValidateEndpointUrl(request.url);
	if (!endpoint)
		return endpoint.error();
	const std::size_t responseLimit = request.maxResponseBytes == 0 ? _options.maxResponseBytes : std::min(request.maxResponseBytes, _options.maxResponseBytes);
	if (responseLimit == 0 || responseLimit > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
		return AiMakeError(AiErrorCode::TransportRejected, "HTTP response size limit is invalid.");

#ifdef _WIN32
	const AiResult<std::wstring> wideHost = toWide(endpoint.value().host);
	if (!wideHost)
		return wideHost.error();
	const AiResult<std::wstring> wideTarget = toWide(endpoint.value().target);
	if (!wideTarget)
		return wideTarget.error();

	std::string serializedHeaders;
	for (const AiHttpHeader & header : request.headers)
	{
		serializedHeaders += header.name;
		serializedHeaders += ": ";
		serializedHeaders += header.value;
		serializedHeaders += "\r\n";
	}
	const AiResult<std::wstring> wideHeaders = toWide(serializedHeaders);
	if (!wideHeaders)
		return wideHeaders.error();
	if (wideHeaders.value().size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) || request.body.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
		return AiMakeError(AiErrorCode::SizeLimitExceeded, "HTTP request exceeds the WinHTTP API size limit.");

	ScopedWinHttpHandle session(::WinHttpOpen(L"Notepad++ AI editing core/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (session.get() == nullptr)
		return winHttpError(stopToken);
	if (!::WinHttpSetTimeouts(session.get(), static_cast<int>(_options.resolveTimeoutMilliseconds), static_cast<int>(_options.connectTimeoutMilliseconds), static_cast<int>(_options.sendTimeoutMilliseconds), static_cast<int>(_options.receiveTimeoutMilliseconds)))
		return winHttpError(stopToken);
	if (stopToken.stop_requested())
		return cancelledError();

	ScopedWinHttpHandle connection(::WinHttpConnect(session.get(), wideHost.value().c_str(), endpoint.value().port, 0));
	if (connection.get() == nullptr)
		return winHttpError(stopToken);
	const DWORD flags = endpoint.value().usesHttps ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET rawRequest = ::WinHttpOpenRequest(connection.get(), L"POST", wideTarget.value().c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (rawRequest == nullptr)
		return winHttpError(stopToken);

	CancellableRequestHandle cancellableRequest;
	std::stop_callback stopCallback(stopToken, [&cancellableRequest]() noexcept { cancellableRequest.cancel(); });
	if (!cancellableRequest.attach(rawRequest))
		return cancelledError();
	DWORD disabledFeatures = WINHTTP_DISABLE_REDIRECTS;
	if (!::WinHttpSetOption(rawRequest, WINHTTP_OPTION_DISABLE_FEATURE, &disabledFeatures, sizeof(disabledFeatures)))
		return winHttpError(stopToken);
	if (stopToken.stop_requested())
		return cancelledError();

	const wchar_t * headerData = wideHeaders.value().empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wideHeaders.value().c_str();
	const DWORD headerLength = static_cast<DWORD>(wideHeaders.value().size());
	LPVOID bodyData = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(request.body.data());
	const DWORD bodyLength = static_cast<DWORD>(request.body.size());
	if (!::WinHttpSendRequest(rawRequest, headerData, headerLength, bodyData, bodyLength, bodyLength, 0))
		return winHttpError(stopToken);
	if (!::WinHttpReceiveResponse(rawRequest, nullptr))
		return winHttpError(stopToken);
	if (stopToken.stop_requested())
		return cancelledError();

	DWORD statusCode = 0;
	DWORD statusSize = sizeof(statusCode);
	if (!::WinHttpQueryHeaders(rawRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX))
		return winHttpError(stopToken);
	DWORD declaredLength = 0;
	DWORD declaredLengthSize = sizeof(declaredLength);
	if (::WinHttpQueryHeaders(rawRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &declaredLength, &declaredLengthSize, WINHTTP_NO_HEADER_INDEX) && declaredLength > responseLimit)
		return AiMakeError(AiErrorCode::SizeLimitExceeded, "HTTP response Content-Length exceeds the configured limit.");

	std::string body;
	while (true)
	{
		if (stopToken.stop_requested())
			return cancelledError();
		DWORD available = 0;
		if (!::WinHttpQueryDataAvailable(rawRequest, &available))
			return winHttpError(stopToken);
		if (available == 0)
			break;
		if (available > responseLimit - body.size())
			return AiMakeError(AiErrorCode::SizeLimitExceeded, "HTTP response exceeds the configured limit.");
		const std::size_t oldSize = body.size();
		body.resize(oldSize + available);
		DWORD received = 0;
		if (!::WinHttpReadData(rawRequest, body.data() + oldSize, available, &received))
			return winHttpError(stopToken);
		body.resize(oldSize + received);
	}
	if (stopToken.stop_requested())
		return cancelledError();
	return AiHttpResponse { statusCode, {}, std::move(body) };
#else
	(void)request;
	(void)endpoint;
	return AiMakeError(AiErrorCode::UnsupportedPlatform, "WinHTTP transport is available only on Windows.");
#endif
}

} // namespace NppAi
