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

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace NppAi
{

enum class AiMessageRole
{
	System,
	User,
	Assistant
};

struct AiMessage
{
	AiMessageRole role = AiMessageRole::User;
	std::string content;
};

struct AiProviderRequest
{
	std::string model;
	std::vector<AiMessage> messages;
	double temperature = 0.0;
	std::size_t maxOutputTokens = 2048;
};

struct AiHttpHeader
{
	std::string name;
	std::string value;
};

struct AiHttpRequest
{
	std::string method = "POST";
	std::string url;
	std::vector<AiHttpHeader> headers;
	std::string body;
	std::size_t maxResponseBytes = 1024 * 1024;
};

struct AiHttpResponse
{
	std::uint32_t statusCode = 0;
	std::vector<AiHttpHeader> headers;
	std::string body;
};

class IAiTransport
{
public:
	virtual ~IAiTransport() = default;
	[[nodiscard]] virtual AiResult<AiHttpResponse> send(const AiHttpRequest & request, std::stop_token stopToken) = 0;
};

enum class AiProviderKind
{
	OpenAiResponses,
	OpenAiCompatibleChatCompletions,
	AzureChatCompletions,
	AnthropicMessages,
	GeminiGenerateContent,
	OllamaChat
};

struct AiProviderConfiguration
{
	AiProviderKind kind = AiProviderKind::OpenAiResponses;
	std::string endpoint;
	std::string defaultModel;
	std::string anthropicVersion = "2023-06-01";
	// Explicit opt-in only; permits a credentialed provider to use an already validated loopback HTTP endpoint.
	bool allowInsecureLoopbackForSecret = false;
};

class IAiProviderAdapter
{
public:
	virtual ~IAiProviderAdapter() = default;
	[[nodiscard]] virtual bool requiresSecret() const noexcept = 0;
	[[nodiscard]] virtual AiResult<AiHttpRequest> serialize(const AiProviderRequest & request, const AiProviderConfiguration & configuration, std::string_view secret) const = 0;
	[[nodiscard]] virtual AiResult<std::string> decode(const AiHttpResponse & response) const = 0;
};

[[nodiscard]] std::unique_ptr<IAiProviderAdapter> AiCreateProviderAdapter(AiProviderKind kind);

class IAiProvider
{
public:
	virtual ~IAiProvider() = default;
	[[nodiscard]] virtual AiResult<std::string> complete(const AiProviderRequest & request, std::stop_token stopToken) = 0;
};

using AiSecretReader = std::function<AiResult<std::string>()>;

// Loads a secret only at request time. Neither this class nor the adapters log request headers or secrets.
class AiHttpProvider final : public IAiProvider
{
public:
	AiHttpProvider(AiProviderConfiguration configuration, std::shared_ptr<IAiTransport> transport, AiSecretReader secretReader = {});
	[[nodiscard]] AiResult<std::string> complete(const AiProviderRequest & request, std::stop_token stopToken) override;

private:
	AiProviderConfiguration _configuration;
	std::shared_ptr<IAiTransport> _transport;
	AiSecretReader _secretReader;
	std::unique_ptr<IAiProviderAdapter> _adapter;
};

// Offline fixtures for deterministic tests. They never perform network I/O.
class AiFakeTransport final : public IAiTransport
{
public:
	void enqueueResponse(AiHttpResponse response);
	void enqueueError(AiError error);
	[[nodiscard]] AiResult<AiHttpResponse> send(const AiHttpRequest & request, std::stop_token stopToken) override;
	[[nodiscard]] std::vector<AiHttpRequest> requests() const;

private:
	mutable std::mutex _mutex;
	std::deque<AiResult<AiHttpResponse>> _responses;
	std::vector<AiHttpRequest> _requests;
};

class AiFakeProvider final : public IAiProvider
{
public:
	void enqueueResponse(std::string response);
	void enqueueError(AiError error);
	[[nodiscard]] AiResult<std::string> complete(const AiProviderRequest & request, std::stop_token stopToken) override;
	[[nodiscard]] std::vector<AiProviderRequest> requests() const;

private:
	mutable std::mutex _mutex;
	std::deque<AiResult<std::string>> _responses;
	std::vector<AiProviderRequest> _requests;
};

using AiRequestId = std::uint64_t;
using AiRequestCallback = std::function<void(AiRequestId, AiResult<std::string>)>;

class IAiRequestManager
{
public:
	virtual ~IAiRequestManager() = default;
	[[nodiscard]] virtual AiResult<AiRequestId> start(AiProviderRequest request, AiRequestCallback callback) = 0;
	// Returns false when the completion callback has already committed to dispatch.
	virtual bool cancel(AiRequestId requestId) noexcept = 0;
	virtual void cancelAll() noexcept = 0;
	// Exposed for deterministic draining; AiRequestManager also reaps automatically.
	virtual void reapCompleted() = 0;
};

// Owns worker lifetimes with std::jthread and a manager-owned reaper. Cancellation is cooperative
// and suppresses callbacks that have not started dispatching; a callback already in progress may finish safely.
class AiRequestManager final : public IAiRequestManager
{
public:
	explicit AiRequestManager(std::shared_ptr<IAiProvider> provider);
	~AiRequestManager() override;
	[[nodiscard]] AiResult<AiRequestId> start(AiProviderRequest request, AiRequestCallback callback) override;
	bool cancel(AiRequestId requestId) noexcept override;
	void cancelAll() noexcept override;
	void reapCompleted() override;

private:
	struct RequestState;
	void notifyCompletion() noexcept;
	void runReaper(std::stop_token stopToken) noexcept;

	std::shared_ptr<IAiProvider> _provider;
	std::mutex _mutex;
	std::unordered_map<AiRequestId, std::shared_ptr<RequestState>> _requests;
	std::atomic<AiRequestId> _nextRequestId = 1;
	std::mutex _completionMutex;
	std::condition_variable_any _completionCondition;
	bool _completionPending = false;
	std::jthread _reaper;
};

} // namespace NppAi
