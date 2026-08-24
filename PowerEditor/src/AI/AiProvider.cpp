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

#include "AiProvider.h"
#include "AiWinHttpTransport.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <thread>
#include <utility>

#include "json.hpp"

namespace NppAi
{
namespace
{

using Json = nlohmann::json;

[[nodiscard]] AiError cancelledError()
{
	return AiMakeError(AiErrorCode::Cancelled, "AI request was cancelled.");
}

[[nodiscard]] bool hasHeaderControlCharacter(std::string_view value) noexcept
{
	for (const char character : value)
	{
		if (character == '\r' || character == '\n' || character == '\0')
			return true;
	}
	return false;
}

[[nodiscard]] AiStatus validateProviderRequest(const AiProviderRequest & request)
{
	if (request.model.empty() || !AiIsValidUtf8(request.model))
		return AiMakeError(AiErrorCode::InvalidArgument, "Provider model must be non-empty valid UTF-8.");
	if (request.messages.empty())
		return AiMakeError(AiErrorCode::InvalidArgument, "Provider request must contain at least one message.");
	if (!std::isfinite(request.temperature) || request.temperature < 0.0 || request.temperature > 2.0)
		return AiMakeError(AiErrorCode::InvalidArgument, "Provider temperature must be finite and between 0 and 2.");
	if (request.maxOutputTokens == 0 || request.maxOutputTokens > 128 * 1024)
		return AiMakeError(AiErrorCode::InvalidArgument, "Provider max-output-token limit is outside the accepted range.");
	for (const AiMessage & message : request.messages)
	{
		if (!AiIsValidUtf8(message.content))
			return AiMakeError(AiErrorCode::InvalidUtf8, "Provider message content is not strict UTF-8.");
	}
	return AiSuccess();
}

[[nodiscard]] std::string roleName(AiMessageRole role)
{
	switch (role)
	{
	case AiMessageRole::System:
		return "system";
	case AiMessageRole::User:
		return "user";
	case AiMessageRole::Assistant:
		return "assistant";
	}
	return "user";
}

[[nodiscard]] AiResult<AiHttpRequest> makeJsonRequest(const AiProviderConfiguration & configuration, Json body, std::vector<AiHttpHeader> headers)
{
	if (configuration.endpoint.empty() || !AiIsValidUtf8(configuration.endpoint))
		return AiMakeError(AiErrorCode::InvalidArgument, "Provider endpoint must be non-empty valid UTF-8.");

	AiHttpRequest request;
	request.method = "POST";
	request.url = configuration.endpoint;
	request.headers.push_back({ "Content-Type", "application/json; charset=utf-8" });
	request.headers.push_back({ "Accept", "application/json" });
	for (AiHttpHeader & header : headers)
		request.headers.push_back(std::move(header));
	try
	{
		request.body = body.dump();
	}
	catch (const std::exception &)
	{
		return AiMakeError(AiErrorCode::InvalidUtf8, "Provider request contains text that cannot be encoded as JSON.");
	}
	return request;
}

[[nodiscard]] AiResult<std::string> parseTextResponse(const AiHttpResponse & response)
{
	if (response.body.empty() || !AiIsValidUtf8(response.body))
		return AiMakeError(AiErrorCode::ParseError, "Provider response is empty or not strict UTF-8 JSON.");
	const Json root = Json::parse(response.body.begin(), response.body.end(), nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return AiMakeError(AiErrorCode::ParseError, "Provider response is not a JSON object.");
	return response.body;
}

[[nodiscard]] AiResult<std::string> textFromValue(const Json & value, std::string_view location)
{
	if (value.is_string())
	{
		const std::string & text = value.get_ref<const std::string &>();
		if (!AiIsValidUtf8(text))
			return AiMakeError(AiErrorCode::InvalidUtf8, "Provider response text is not strict UTF-8.");
		return text;
	}
	if (!value.is_array())
		return AiMakeError(AiErrorCode::ParseError, std::string(location) + " does not contain text.");

	std::string result;
	for (const Json & part : value)
	{
		if (part.is_string())
		{
			result += part.get_ref<const std::string &>();
			continue;
		}
		if (!part.is_object())
			continue;
		const auto text = part.find("text");
		if (text != part.end() && text->is_string())
			result += text->get_ref<const std::string &>();
	}
	if (result.empty() || !AiIsValidUtf8(result))
		return AiMakeError(AiErrorCode::ParseError, std::string(location) + " contains no usable strict UTF-8 text.");
	return result;
}

[[nodiscard]] AiResult<std::string> decodeOpenAiResponses(const AiHttpResponse & response)
{
	const AiResult<std::string> raw = parseTextResponse(response);
	if (!raw)
		return raw.error();
	const Json root = Json::parse(raw.value().begin(), raw.value().end(), nullptr, false);
	const auto direct = root.find("output_text");
	if (direct != root.end())
		return textFromValue(*direct, "OpenAI Responses output_text");
	const auto output = root.find("output");
	if (output == root.end() || !output->is_array())
		return AiMakeError(AiErrorCode::ParseError, "OpenAI Responses response contains no output array.");

	std::string result;
	for (const Json & item : *output)
	{
		if (!item.is_object())
			continue;
		const auto content = item.find("content");
		if (content == item.end())
			continue;
		const AiResult<std::string> text = textFromValue(*content, "OpenAI Responses content");
		if (text)
			result += text.value();
	}
	if (result.empty() || !AiIsValidUtf8(result))
		return AiMakeError(AiErrorCode::ParseError, "OpenAI Responses response contains no output text.");
	return result;
}

[[nodiscard]] AiResult<std::string> decodeOpenAiChat(const AiHttpResponse & response)
{
	const AiResult<std::string> raw = parseTextResponse(response);
	if (!raw)
		return raw.error();
	const Json root = Json::parse(raw.value().begin(), raw.value().end(), nullptr, false);
	const auto choices = root.find("choices");
	if (choices == root.end() || !choices->is_array() || choices->empty() || !(*choices)[0].is_object())
		return AiMakeError(AiErrorCode::ParseError, "Chat Completions response contains no choice.");
	const auto message = (*choices)[0].find("message");
	if (message == (*choices)[0].end() || !message->is_object())
		return AiMakeError(AiErrorCode::ParseError, "Chat Completions response contains no message.");
	const auto content = message->find("content");
	if (content == message->end())
		return AiMakeError(AiErrorCode::ParseError, "Chat Completions response contains no content.");
	return textFromValue(*content, "Chat Completions content");
}

[[nodiscard]] AiResult<std::string> decodeAnthropic(const AiHttpResponse & response)
{
	const AiResult<std::string> raw = parseTextResponse(response);
	if (!raw)
		return raw.error();
	const Json root = Json::parse(raw.value().begin(), raw.value().end(), nullptr, false);
	const auto content = root.find("content");
	if (content == root.end())
		return AiMakeError(AiErrorCode::ParseError, "Anthropic response contains no content.");
	return textFromValue(*content, "Anthropic content");
}

[[nodiscard]] AiResult<std::string> decodeGemini(const AiHttpResponse & response)
{
	const AiResult<std::string> raw = parseTextResponse(response);
	if (!raw)
		return raw.error();
	const Json root = Json::parse(raw.value().begin(), raw.value().end(), nullptr, false);
	const auto candidates = root.find("candidates");
	if (candidates == root.end() || !candidates->is_array() || candidates->empty() || !(*candidates)[0].is_object())
		return AiMakeError(AiErrorCode::ParseError, "Gemini response contains no candidate.");
	const auto content = (*candidates)[0].find("content");
	if (content == (*candidates)[0].end() || !content->is_object())
		return AiMakeError(AiErrorCode::ParseError, "Gemini response contains no candidate content.");
	const auto parts = content->find("parts");
	if (parts == content->end())
		return AiMakeError(AiErrorCode::ParseError, "Gemini response contains no candidate parts.");
	return textFromValue(*parts, "Gemini candidate parts");
}

[[nodiscard]] AiResult<std::string> decodeOllama(const AiHttpResponse & response)
{
	const AiResult<std::string> raw = parseTextResponse(response);
	if (!raw)
		return raw.error();
	const Json root = Json::parse(raw.value().begin(), raw.value().end(), nullptr, false);
	const auto message = root.find("message");
	if (message == root.end() || !message->is_object())
		return AiMakeError(AiErrorCode::ParseError, "Ollama response contains no message.");
	const auto content = message->find("content");
	if (content == message->end())
		return AiMakeError(AiErrorCode::ParseError, "Ollama response contains no content.");
	return textFromValue(*content, "Ollama content");
}

class OpenAiResponsesAdapter final : public IAiProviderAdapter
{
public:
	[[nodiscard]] bool requiresSecret() const noexcept override
	{
		return true;
	}

	[[nodiscard]] AiResult<AiHttpRequest> serialize(const AiProviderRequest & request, const AiProviderConfiguration & configuration, std::string_view secret) const override
	{
		if (secret.empty() || !AiIsValidUtf8(secret) || hasHeaderControlCharacter(secret))
			return AiMakeError(AiErrorCode::SecretUnavailable, "OpenAI credential is unavailable or invalid.");
		Json body;
		body["model"] = request.model;
		body["temperature"] = request.temperature;
		body["max_output_tokens"] = request.maxOutputTokens;
		body["input"] = Json::array();
		for (const AiMessage & message : request.messages)
		{
			Json input;
			input["role"] = roleName(message.role);
			input["content"] = Json::array();
			input["content"].push_back({ { "type", "input_text" }, { "text", message.content } });
			body["input"].push_back(std::move(input));
		}
		return makeJsonRequest(configuration, std::move(body), { { "Authorization", "Bearer " + std::string(secret) } });
	}

	[[nodiscard]] AiResult<std::string> decode(const AiHttpResponse & response) const override
	{
		return decodeOpenAiResponses(response);
	}
};

class OpenAiChatAdapter final : public IAiProviderAdapter
{
public:
	explicit OpenAiChatAdapter(bool azure) : _azure(azure)
	{
	}

	[[nodiscard]] bool requiresSecret() const noexcept override
	{
		return true;
	}

	[[nodiscard]] AiResult<AiHttpRequest> serialize(const AiProviderRequest & request, const AiProviderConfiguration & configuration, std::string_view secret) const override
	{
		if (secret.empty() || !AiIsValidUtf8(secret) || hasHeaderControlCharacter(secret))
			return AiMakeError(AiErrorCode::SecretUnavailable, "Chat Completions credential is unavailable or invalid.");
		Json body;
		body["model"] = request.model;
		body["temperature"] = request.temperature;
		body["max_tokens"] = request.maxOutputTokens;
		body["messages"] = Json::array();
		for (const AiMessage & message : request.messages)
			body["messages"].push_back({ { "role", roleName(message.role) }, { "content", message.content } });
		const AiHttpHeader authorization = _azure ? AiHttpHeader { "api-key", std::string(secret) } : AiHttpHeader { "Authorization", "Bearer " + std::string(secret) };
		return makeJsonRequest(configuration, std::move(body), { authorization });
	}

	[[nodiscard]] AiResult<std::string> decode(const AiHttpResponse & response) const override
	{
		return decodeOpenAiChat(response);
	}

private:
	bool _azure;
};

class AnthropicAdapter final : public IAiProviderAdapter
{
public:
	[[nodiscard]] bool requiresSecret() const noexcept override
	{
		return true;
	}

	[[nodiscard]] AiResult<AiHttpRequest> serialize(const AiProviderRequest & request, const AiProviderConfiguration & configuration, std::string_view secret) const override
	{
		if (secret.empty() || !AiIsValidUtf8(secret) || hasHeaderControlCharacter(secret))
			return AiMakeError(AiErrorCode::SecretUnavailable, "Anthropic credential is unavailable or invalid.");
		Json body;
		body["model"] = request.model;
		body["temperature"] = request.temperature;
		body["max_tokens"] = request.maxOutputTokens;
		body["messages"] = Json::array();
		std::string system;
		for (const AiMessage & message : request.messages)
		{
			if (message.role == AiMessageRole::System)
			{
				if (!system.empty())
					system += "\n\n";
				system += message.content;
				continue;
			}
			const std::string role = message.role == AiMessageRole::Assistant ? "assistant" : "user";
			body["messages"].push_back({ { "role", role }, { "content", message.content } });
		}
		if (!system.empty())
			body["system"] = system;
		return makeJsonRequest(configuration, std::move(body), { { "x-api-key", std::string(secret) }, { "anthropic-version", configuration.anthropicVersion } });
	}

	[[nodiscard]] AiResult<std::string> decode(const AiHttpResponse & response) const override
	{
		return decodeAnthropic(response);
	}
};

class GeminiAdapter final : public IAiProviderAdapter
{
public:
	[[nodiscard]] bool requiresSecret() const noexcept override
	{
		return true;
	}

	[[nodiscard]] AiResult<AiHttpRequest> serialize(const AiProviderRequest & request, const AiProviderConfiguration & configuration, std::string_view secret) const override
	{
		if (secret.empty() || !AiIsValidUtf8(secret) || hasHeaderControlCharacter(secret))
			return AiMakeError(AiErrorCode::SecretUnavailable, "Gemini credential is unavailable or invalid.");
		Json body;
		body["contents"] = Json::array();
		Json systemParts = Json::array();
		for (const AiMessage & message : request.messages)
		{
			if (message.role == AiMessageRole::System)
			{
				systemParts.push_back({ { "text", message.content } });
				continue;
			}
			const std::string role = message.role == AiMessageRole::Assistant ? "model" : "user";
			Json content;
			content["role"] = role;
			content["parts"] = Json::array();
			content["parts"].push_back({ { "text", message.content } });
			body["contents"].push_back(std::move(content));
		}
		if (!systemParts.empty())
			body["systemInstruction"] = { { "parts", systemParts } };
		body["generationConfig"] = { { "temperature", request.temperature }, { "maxOutputTokens", request.maxOutputTokens } };
		return makeJsonRequest(configuration, std::move(body), { { "x-goog-api-key", std::string(secret) } });
	}

	[[nodiscard]] AiResult<std::string> decode(const AiHttpResponse & response) const override
	{
		return decodeGemini(response);
	}
};

class OllamaAdapter final : public IAiProviderAdapter
{
public:
	[[nodiscard]] bool requiresSecret() const noexcept override
	{
		return false;
	}

	[[nodiscard]] AiResult<AiHttpRequest> serialize(const AiProviderRequest & request, const AiProviderConfiguration & configuration, std::string_view) const override
	{
		Json body;
		body["model"] = request.model;
		body["stream"] = false;
		body["messages"] = Json::array();
		for (const AiMessage & message : request.messages)
			body["messages"].push_back({ { "role", roleName(message.role) }, { "content", message.content } });
		body["options"] = { { "temperature", request.temperature }, { "num_predict", request.maxOutputTokens } };
		return makeJsonRequest(configuration, std::move(body), {});
	}

	[[nodiscard]] AiResult<std::string> decode(const AiHttpResponse & response) const override
	{
		return decodeOllama(response);
	}
};

} // namespace

std::unique_ptr<IAiProviderAdapter> AiCreateProviderAdapter(AiProviderKind kind)
{
	switch (kind)
	{
	case AiProviderKind::OpenAiResponses:
		return std::make_unique<OpenAiResponsesAdapter>();
	case AiProviderKind::OpenAiCompatibleChatCompletions:
		return std::make_unique<OpenAiChatAdapter>(false);
	case AiProviderKind::AzureChatCompletions:
		return std::make_unique<OpenAiChatAdapter>(true);
	case AiProviderKind::AnthropicMessages:
		return std::make_unique<AnthropicAdapter>();
	case AiProviderKind::GeminiGenerateContent:
		return std::make_unique<GeminiAdapter>();
	case AiProviderKind::OllamaChat:
		return std::make_unique<OllamaAdapter>();
	}
	return nullptr;
}

AiHttpProvider::AiHttpProvider(AiProviderConfiguration configuration, std::shared_ptr<IAiTransport> transport, AiSecretReader secretReader) :
	_configuration(std::move(configuration)),
	_transport(std::move(transport)),
	_secretReader(std::move(secretReader)),
	_adapter(AiCreateProviderAdapter(_configuration.kind))
{
}

AiResult<std::string> AiHttpProvider::complete(const AiProviderRequest & request, std::stop_token stopToken)
{
	if (stopToken.stop_requested())
		return cancelledError();
	if (!_transport || !_adapter)
		return AiMakeError(AiErrorCode::InternalError, "AI provider is not configured with a transport and adapter.");

	AiProviderRequest effectiveRequest = request;
	if (effectiveRequest.model.empty())
		effectiveRequest.model = _configuration.defaultModel;
	const AiStatus requestStatus = validateProviderRequest(effectiveRequest);
	if (!requestStatus)
		return requestStatus.error();

	std::string secret;
	if (_adapter->requiresSecret())
	{
		if (!_secretReader)
			return AiMakeError(AiErrorCode::SecretUnavailable, "AI provider credential is unavailable.");
		AiResult<std::string> loadedSecret = _secretReader();
		if (!loadedSecret || loadedSecret.value().empty() || !AiIsValidUtf8(loadedSecret.value()) || hasHeaderControlCharacter(loadedSecret.value()))
			return AiMakeError(AiErrorCode::SecretUnavailable, "AI provider credential is unavailable.");
		secret = loadedSecret.value();
		std::fill(loadedSecret.value().begin(), loadedSecret.value().end(), '\0');
		loadedSecret.value().clear();
	}
	if (stopToken.stop_requested())
		return cancelledError();

	AiResult<AiHttpRequest> wireRequest = _adapter->serialize(effectiveRequest, _configuration, secret);
	std::fill(secret.begin(), secret.end(), '\0');
	secret.clear();
	if (!wireRequest)
		return wireRequest.error();
	const AiResult<AiValidatedEndpoint> endpoint = AiValidateEndpointUrl(wireRequest.value().url);
	if (!endpoint)
		return endpoint.error();
	if (_adapter->requiresSecret() && !endpoint.value().usesHttps && !_configuration.allowInsecureLoopbackForSecret)
		return AiMakeError(AiErrorCode::TransportRejected, "A credentialed provider requires HTTPS unless loopback HTTP is explicitly enabled.");
	AiResult<AiHttpResponse> wireResponse = _transport->send(wireRequest.value(), stopToken);
	for (AiHttpHeader & header : wireRequest.value().headers)
	{
		std::fill(header.value.begin(), header.value.end(), '\0');
		header.value.clear();
	}
	if (!wireResponse)
	{
		if (wireResponse.error().code == AiErrorCode::Cancelled || stopToken.stop_requested())
			return cancelledError();
		return AiMakeError(wireResponse.error().code, "AI provider request failed: " + wireResponse.error().message);
	}
	if (wireResponse.value().statusCode < 200 || wireResponse.value().statusCode >= 300)
		return AiMakeError(AiErrorCode::HttpFailure, "AI provider returned HTTP status " + std::to_string(wireResponse.value().statusCode) + ".");
	if (stopToken.stop_requested())
		return cancelledError();
	return _adapter->decode(wireResponse.value());
}

void AiFakeTransport::enqueueResponse(AiHttpResponse response)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_responses.emplace_back(std::move(response));
}

void AiFakeTransport::enqueueError(AiError error)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_responses.emplace_back(std::move(error));
}

AiResult<AiHttpResponse> AiFakeTransport::send(const AiHttpRequest & request, std::stop_token stopToken)
{
	if (stopToken.stop_requested())
		return cancelledError();
	std::lock_guard<std::mutex> lock(_mutex);
	_requests.push_back(request);
	if (_responses.empty())
		return AiMakeError(AiErrorCode::TransportFailure, "Fake transport has no queued response.");
	AiResult<AiHttpResponse> response = std::move(_responses.front());
	_responses.pop_front();
	return response;
}

std::vector<AiHttpRequest> AiFakeTransport::requests() const
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _requests;
}

void AiFakeProvider::enqueueResponse(std::string response)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_responses.emplace_back(std::move(response));
}

void AiFakeProvider::enqueueError(AiError error)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_responses.emplace_back(std::move(error));
}

AiResult<std::string> AiFakeProvider::complete(const AiProviderRequest & request, std::stop_token stopToken)
{
	if (stopToken.stop_requested())
		return cancelledError();
	std::lock_guard<std::mutex> lock(_mutex);
	_requests.push_back(request);
	if (_responses.empty())
		return AiMakeError(AiErrorCode::TransportFailure, "Fake provider has no queued response.");
	AiResult<std::string> response = std::move(_responses.front());
	_responses.pop_front();
	return response;
}

std::vector<AiProviderRequest> AiFakeProvider::requests() const
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _requests;
}

struct AiRequestManager::RequestState
{
	std::jthread worker;
	std::atomic<bool> cancelled = false;
	std::atomic<bool> started = false;
	std::atomic<bool> done = false;
	std::mutex dispatchMutex;
	bool callbackCommitted = false;
};

AiRequestManager::AiRequestManager(std::shared_ptr<IAiProvider> provider) : _provider(std::move(provider))
{
	_reaper = std::jthread([this](std::stop_token stopToken) { runReaper(stopToken); });
}

AiRequestManager::~AiRequestManager()
{
	cancelAll();
	_reaper.request_stop();
	_completionCondition.notify_all();
	if (_reaper.joinable())
		_reaper.join();

	std::unordered_map<AiRequestId, std::shared_ptr<RequestState>> requests;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		requests.swap(_requests);
	}
}

AiResult<AiRequestId> AiRequestManager::start(AiProviderRequest request, AiRequestCallback callback)
{
	if (!_provider)
		return AiMakeError(AiErrorCode::InvalidArgument, "Request manager requires a provider.");
	if (!callback)
		return AiMakeError(AiErrorCode::InvalidArgument, "Request manager requires a completion callback.");

	reapCompleted();
	const AiRequestId requestId = _nextRequestId.fetch_add(1);
	if (requestId == 0)
		return AiMakeError(AiErrorCode::InternalError, "Request identifier space is exhausted.");
	const std::shared_ptr<RequestState> state = std::make_shared<RequestState>();
	const std::shared_ptr<IAiProvider> provider = _provider;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_requests.emplace(requestId, state);
	}

	try
	{
		{
			std::lock_guard<std::mutex> dispatchLock(state->dispatchMutex);
			state->worker = std::jthread([this, provider, state, request = std::move(request), callback = std::move(callback), requestId](std::stop_token stopToken) mutable {
			AiResult<std::string> result = AiMakeError(AiErrorCode::InternalError, "AI request did not produce a result.");
			try
			{
				result = provider->complete(request, stopToken);
			}
			catch (const std::exception &)
			{
				result = AiMakeError(AiErrorCode::InternalError, "AI provider threw while processing a request.");
			}
			catch (...)
			{
				result = AiMakeError(AiErrorCode::InternalError, "AI provider threw an unknown exception.");
			}

			if (stopToken.stop_requested() || state->cancelled.load())
				result = cancelledError();
			bool dispatchCallback = false;
			{
				std::lock_guard<std::mutex> dispatchLock(state->dispatchMutex);
				if (!state->cancelled.load() && !stopToken.stop_requested())
				{
					state->callbackCommitted = true;
					dispatchCallback = true;
				}
			}
			if (dispatchCallback)
			{
				try
				{
					callback(requestId, std::move(result));
				}
				catch (...)
				{
					// User callbacks must not terminate the request worker.
				}
			}
			state->done.store(true);
			notifyCompletion();
		});
			state->started.store(true);
			if (state->cancelled.load())
				state->worker.request_stop();
		}
		notifyCompletion();
	}
	catch (const std::exception &)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_requests.erase(requestId);
		return AiMakeError(AiErrorCode::InternalError, "Unable to start the AI request worker.");
	}

	return requestId;
}

bool AiRequestManager::cancel(AiRequestId requestId) noexcept
{
	std::shared_ptr<RequestState> state;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		const auto iterator = _requests.find(requestId);
		if (iterator == _requests.end())
			return false;
		state = iterator->second;
	}
	{
		std::lock_guard<std::mutex> dispatchLock(state->dispatchMutex);
		if (state->callbackCommitted)
			return false;
		state->cancelled.store(true);
	}
	state->worker.request_stop();
	return true;
}

void AiRequestManager::cancelAll() noexcept
{
	std::lock_guard<std::mutex> lock(_mutex);
	for (const auto & request : _requests)
	{
		{
			std::lock_guard<std::mutex> dispatchLock(request.second->dispatchMutex);
			if (!request.second->callbackCommitted)
				request.second->cancelled.store(true);
		}
		request.second->worker.request_stop();
	}
}

void AiRequestManager::reapCompleted()
{
	std::vector<std::shared_ptr<RequestState>> completed;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		for (auto iterator = _requests.begin(); iterator != _requests.end();)
		{
			if (iterator->second->started.load() && iterator->second->done.load())
			{
				completed.push_back(std::move(iterator->second));
				iterator = _requests.erase(iterator);
			}
			else
			{
				++iterator;
			}
		}
	}
}

void AiRequestManager::notifyCompletion() noexcept
{
	{
		std::lock_guard<std::mutex> lock(_completionMutex);
		_completionPending = true;
	}
	_completionCondition.notify_one();
}

void AiRequestManager::runReaper(std::stop_token stopToken) noexcept
{
	std::unique_lock<std::mutex> lock(_completionMutex);
	while (!stopToken.stop_requested())
	{
		_completionCondition.wait(lock, [this, &stopToken]() {
			return stopToken.stop_requested() || _completionPending;
		});
		if (stopToken.stop_requested())
			break;
		_completionPending = false;
		lock.unlock();
		reapCompleted();
		lock.lock();
	}
}

} // namespace NppAi
