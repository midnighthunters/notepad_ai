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

#include "AiNotepadCommand.h"

#include "AiCore.h"
#include "AiProvider.h"
#include "AiSecretStore.h"
#include "AiWinHttpTransport.h"

#include "Buffer.h"
#include "ScintillaEditView.h"
#include "UserDefineDialog.h"
#include "Parameters.h"

#include <Scintilla.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <wincred.h>

namespace NppAi
{
namespace
{

constexpr wchar_t geminiCredentialTarget[] = L"Notepad++ AI/Gemini";
constexpr wchar_t openRouterCredentialTarget[] = L"Notepad++ AI/OpenRouter";
constexpr char defaultGeminiModel[] = "gemini-flash-latest";
constexpr char defaultOpenRouterModel[] = "stealth/ox-alpha";
constexpr char defaultGeminiEndpointTemplate[] = "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent";
constexpr char defaultOpenRouterEndpoint[] = "https://openrouter.ai/api/v1/chat/completions";

[[nodiscard]] std::string toUtf8(std::wstring_view text)
{
	if (text.empty())
		return {};
	const int size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	if (size <= 0)
		return {};
	std::string utf8(static_cast<std::size_t>(size), '\0');
	if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr) != size)
		return {};
	return utf8;
}

[[nodiscard]] std::wstring toWide(std::string_view text)
{
	if (text.empty())
		return {};
	const int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (size <= 0)
		return L"AI returned text that is not valid UTF-8.";
	std::wstring wide(static_cast<std::size_t>(size), L'\0');
	if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size) != size)
		return L"AI returned text that is not valid UTF-8.";
	return wide;
}


[[nodiscard]] AiProviderKind configuredProviderKind()
{
	const NppGUI & nppGUI = NppParameters::getInstance().getNppGUI();
	return nppGUI._aiProvider == L"OpenRouter" ? AiProviderKind::OpenAiCompatibleChatCompletions : AiProviderKind::GeminiGenerateContent;
}

[[nodiscard]] bool isValidModelName(std::string_view model, AiProviderKind kind) noexcept
{
	if (model.empty() || model.size() > 128)
		return false;
	for (const char character : model)
	{
		const bool common = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '-' || character == '.' || character == '_';
		const bool openRouterExtra = character == '/' || character == ':' || character == '@';
		if (!common && (kind != AiProviderKind::OpenAiCompatibleChatCompletions || !openRouterExtra))
			return false;
	}
	return true;
}

[[nodiscard]] std::string configuredModel(AiProviderKind kind)
{
	const NppGUI & nppGUI = NppParameters::getInstance().getNppGUI();
	const std::string model = toUtf8(nppGUI._aiModel);
	const char * fallback = kind == AiProviderKind::OpenAiCompatibleChatCompletions ? defaultOpenRouterModel : defaultGeminiModel;
	return isValidModelName(model, kind) ? model : fallback;
}

[[nodiscard]] std::string configuredEndpoint(AiProviderKind kind, const std::string & model)
{
	const NppGUI & nppGUI = NppParameters::getInstance().getNppGUI();
	std::string endpoint = toUtf8(nppGUI._aiEndpoint);
	if (endpoint.empty() || !AiIsValidUtf8(endpoint))
		endpoint = kind == AiProviderKind::OpenAiCompatibleChatCompletions ? defaultOpenRouterEndpoint : defaultGeminiEndpointTemplate;

	if (kind == AiProviderKind::GeminiGenerateContent)
	{
		const std::string marker = "{model}";
		std::size_t position = endpoint.find(marker);
		while (position != std::string::npos)
		{
			endpoint.replace(position, marker.size(), model);
			position = endpoint.find(marker, position + model.size());
		}
	}
	return endpoint;
}

struct RuntimeConfiguration
{
	AiProviderKind kind = AiProviderKind::GeminiGenerateContent;
	std::string model;
	std::string endpoint;
	const wchar_t * credentialTarget = geminiCredentialTarget;
	const wchar_t * providerName = L"Gemini";
};

[[nodiscard]] RuntimeConfiguration currentConfiguration()
{
	RuntimeConfiguration configuration;
	configuration.kind = configuredProviderKind();
	configuration.model = configuredModel(configuration.kind);
	configuration.endpoint = configuredEndpoint(configuration.kind, configuration.model);
	if (configuration.kind == AiProviderKind::OpenAiCompatibleChatCompletions)
	{
		configuration.credentialTarget = openRouterCredentialTarget;
		configuration.providerName = L"OpenRouter";
	}
	return configuration;
}

void showError(HWND parent, const AiError & error)
{
	const std::wstring message = toWide(error.message);
	::MessageBoxW(parent, message.c_str(), L"Notepad++ AI", MB_OK | MB_ICONERROR);
}
[[nodiscard]] AiResult<std::string> promptForSecret(HWND parent)
{
	const RuntimeConfiguration configuration = currentConfiguration();
	wchar_t userName[CREDUI_MAX_USERNAME_LENGTH + 1]{};
	wchar_t password[CREDUI_MAX_PASSWORD_LENGTH + 1]{};
	BOOL save = FALSE;
	CREDUI_INFOW info{};
	info.cbSize = sizeof(info);
	info.hwndParent = parent;
	const std::wstring prompt = L"Enter your " + std::wstring(configuration.providerName) + L" API key. It will be stored only in Windows Credential Manager.";
	info.pszMessageText = prompt.c_str();
	info.pszCaptionText = L"Notepad++ AI setup";
	const DWORD result = ::CredUIPromptForCredentialsW(
		&info,
		configuration.credentialTarget,
		nullptr,
		0,
		userName,
		static_cast<ULONG>(CREDUI_MAX_USERNAME_LENGTH + 1),
		password,
		static_cast<ULONG>(CREDUI_MAX_PASSWORD_LENGTH + 1),
		&save,
		CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_ALWAYS_SHOW_UI | CREDUI_FLAGS_DO_NOT_PERSIST);
	if (result != NO_ERROR)
		return AiMakeError(AiErrorCode::SecretUnavailable, "No AI provider API key is available.");

	std::string secret = toUtf8(password);
	::SecureZeroMemory(password, sizeof(password));
	if (secret.empty())
		return AiMakeError(AiErrorCode::SecretUnavailable, "The AI provider API key cannot be empty.");
	return secret;
}

[[nodiscard]] AiResult<std::string> loadOrPromptForSecret(HWND parent)
{
	const RuntimeConfiguration configuration = currentConfiguration();
	WindowsCredentialManagerSecretStore store;
	const AiResult<std::string> existing = store.load(configuration.credentialTarget);
	if (existing && !existing.value().empty())
		return existing.value();

	AiResult<std::string> prompted = promptForSecret(parent);
	if (!prompted)
		return prompted.error();
	const AiStatus saved = store.save(configuration.credentialTarget, prompted.value());
	if (!saved)
	{
		std::fill(prompted.value().begin(), prompted.value().end(), '\0');
		return saved.error();
	}
	return prompted.value();
}

[[nodiscard]] AiResult<AiDocumentSnapshot> captureSnapshot(ScintillaEditView & view, Buffer * expectedBuffer)
{
	if (view.getCurrentBuffer() != expectedBuffer)
		return AiMakeError(AiErrorCode::StaleDocument, "The active document changed while the AI request was in progress.");
	if (view.execute(SCI_GETCODEPAGE) != SC_CP_UTF8)
		return AiMakeError(AiErrorCode::InvalidUtf8, "AI editing is currently available only for UTF-8 documents.");
	const auto rawLength = view.execute(SCI_GETTEXTLENGTH);
	if (rawLength < 0)
		return AiMakeError(AiErrorCode::EditorRejected, "Unable to read the current document.");
	const std::size_t length = static_cast<std::size_t>(rawLength);
	std::vector<char> text(length + 1, '\0');
	view.execute(SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(text.data()));
	std::string identity = "buffer:" + std::to_string(reinterpret_cast<std::uintptr_t>(expectedBuffer));
	return AiMakeDocumentSnapshot(std::string(text.data(), length), std::move(identity));
}

class ScintillaAiEditor final : public IAiEditor
{
public:
	ScintillaAiEditor(ScintillaEditView & view, Buffer * buffer) : _view(view), _buffer(buffer)
	{
	}

	[[nodiscard]] AiResult<AiDocumentSnapshot> captureSnapshot() const override
	{
		return ::NppAi::captureSnapshot(_view, _buffer);
	}

	[[nodiscard]] AiStatus canReplaceRange(const AiTextRange & range, std::string_view replacement) override
	{
		if (_view.getCurrentBuffer() != _buffer || _buffer == nullptr || _buffer->isReadOnly())
			return AiMakeError(AiErrorCode::EditorRejected, "The target document is no longer writable.");
		if (_view.execute(SCI_GETCODEPAGE) != SC_CP_UTF8)
			return AiMakeError(AiErrorCode::InvalidUtf8, "AI editing is currently available only for UTF-8 documents.");
		const auto rawLength = _view.execute(SCI_GETTEXTLENGTH);
		if (rawLength < 0 || range.end > static_cast<std::size_t>(rawLength) || !range.isOrdered() || !AiIsValidUtf8(replacement))
			return AiMakeError(AiErrorCode::InvalidRange, "An approved AI edit can no longer be applied to this document.");
		return AiSuccess();
	}

	[[nodiscard]] AiStatus beginUndoAction() override
	{
		_view.execute(SCI_BEGINUNDOACTION);
		_undoOpen = true;
		return AiSuccess();
	}

	void endUndoAction() noexcept override
	{
		if (_undoOpen)
		{
			_view.execute(SCI_ENDUNDOACTION);
			_undoOpen = false;
		}
	}

	void abortUndoAction() noexcept override
	{
		if (_undoOpen)
		{
			_view.execute(SCI_ENDUNDOACTION);
			_view.execute(SCI_UNDO);
			_undoOpen = false;
		}
	}

	[[nodiscard]] AiStatus replaceRange(const AiTextRange & range, std::string_view replacement) override
	{
		_view.execute(SCI_SETTARGETRANGE, range.start, range.end);
		const auto result = _view.execute(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
		if (result < 0)
			return AiMakeError(AiErrorCode::EditorRejected, "Scintilla rejected an approved AI edit.");
		return AiSuccess();
	}

private:
	ScintillaEditView & _view;
	Buffer * _buffer = nullptr;
	bool _undoOpen = false;
};

[[nodiscard]] std::wstring makeActivityMessage(const RuntimeConfiguration & runtime, const AiScope & scope)
{
	const char * scopeName = scope.kind == AiScopeKind::Selection ? "selection" : scope.kind == AiScopeKind::LineRange ? "line range" : "document";
	const std::string activity =
		"AI activity\n\n"
		"[done] Captured a protected UTF-8 snapshot\n"
		"[done] Restricted changes to the " + std::string(scopeName) + "\n"
		"[working] Sending a secure request to " + toUtf8(runtime.providerName) + "\n"
		"[working] Waiting for a structured edit plan\n\n"
		"Validated changes will apply automatically and remain one-step undoable.";
	return toWide(activity);
}

// A small owner-modal popup shown while a network request runs on a background thread. It keeps the
// UI responsive (the message loop below keeps pumping) and lets the user cancel a slow request,
// which closes the WinHTTP handle through the transport's stop-callback.
class AiProgressModal final
{
public:
	AiProgressModal(HWND owner, HINSTANCE instance, const std::wstring & caption, const std::wstring & message) :
		_owner(owner), _instance(instance)
	{
		ensureClassRegistered(instance);

		const int width = 460;
		const int height = 190;
		int x = CW_USEDEFAULT;
		int y = CW_USEDEFAULT;
		RECT ownerRect{};
		if (owner != nullptr && ::GetWindowRect(owner, &ownerRect))
		{
			x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
			y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
		}

		_window = ::CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, className(), caption.c_str(),
			WS_POPUP | WS_CAPTION, x, y, width, height, owner, nullptr, instance, nullptr);
		if (_window == nullptr)
			return;
		::SetWindowLongPtrW(_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		RECT client{};
		::GetClientRect(_window, &client);
		const int clientWidth = client.right - client.left;
		_text = ::CreateWindowExW(0, L"STATIC", message.c_str(), WS_CHILD | WS_VISIBLE,
			16, 16, clientWidth - 32, 118, _window, nullptr, instance, nullptr);
		const int buttonWidth = 84;
		const int buttonHeight = 24;
		_cancelButton = ::CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			(clientWidth - buttonWidth) / 2, 142, buttonWidth, buttonHeight, _window, reinterpret_cast<HMENU>(IDCANCEL), instance, nullptr);

		HFONT font = reinterpret_cast<HFONT>(::SendMessageW(owner != nullptr ? owner : _window, WM_GETFONT, 0, 0));
		if (font == nullptr)
			font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
		if (font != nullptr)
		{
			::SendMessageW(_text, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
			::SendMessageW(_cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
		}
		::ShowWindow(_window, SW_SHOW);
		::UpdateWindow(_window);
	}

	~AiProgressModal()
	{
		if (_window != nullptr)
			::DestroyWindow(_window);
	}

	AiProgressModal(const AiProgressModal &) = delete;
	AiProgressModal & operator=(const AiProgressModal &) = delete;

	// Pumps messages until doneEvent is signalled. Returns true on normal completion, false if the
	// user cancelled (in which case the stop-source has been asked to stop).
	[[nodiscard]] bool waitFor(HANDLE doneEvent, std::stop_source & stopSource)
	{
		bool cancelled = false;
		for (;;)
		{
			const DWORD waitResult = ::MsgWaitForMultipleObjects(1, &doneEvent, FALSE, INFINITE, QS_ALLINPUT);
			if (waitResult == WAIT_OBJECT_0)
				break;

			MSG msg{};
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					if (!cancelled)
					{
						cancelled = true;
						stopSource.request_stop();
					}
					::PostQuitMessage(static_cast<int>(msg.wParam));
					return false;
				}
				if (_window == nullptr || !::IsDialogMessageW(_window, &msg))
				{
					::TranslateMessage(&msg);
					::DispatchMessageW(&msg);
				}
			}

			if (_cancelRequested && !cancelled)
			{
				cancelled = true;
				stopSource.request_stop();
				if (_text != nullptr)
					::SetWindowTextW(_text, L"Cancelling the request…");
				if (_cancelButton != nullptr)
					::EnableWindow(_cancelButton, FALSE);
			}
		}
		return !cancelled;
	}

private:
	static const wchar_t * className() noexcept
	{
		return L"NppAiProgressModal";
	}

	static void ensureClassRegistered(HINSTANCE instance)
	{
		static std::once_flag onceFlag;
		std::call_once(onceFlag, [instance]() {
			WNDCLASSEXW windowClass{};
			windowClass.cbSize = sizeof(windowClass);
			windowClass.lpfnWndProc = &AiProgressModal::windowProc;
			windowClass.hInstance = instance;
			windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
			windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
			windowClass.lpszClassName = className();
			::RegisterClassExW(&windowClass);
		});
	}

	static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		AiProgressModal * self = reinterpret_cast<AiProgressModal *>(::GetWindowLongPtrW(window, GWLP_USERDATA));
		switch (message)
		{
		case WM_COMMAND:
			if (LOWORD(wParam) == IDCANCEL)
			{
				if (self != nullptr)
					self->_cancelRequested = true;
				return 0;
			}
			break;
		case WM_CLOSE:
			if (self != nullptr)
				self->_cancelRequested = true;
			return 0;
		default:
			break;
		}
		return ::DefWindowProcW(window, message, wParam, lParam);
	}

	HWND _owner = nullptr;
	HINSTANCE _instance = nullptr;
	HWND _window = nullptr;
	HWND _text = nullptr;
	HWND _cancelButton = nullptr;
	bool _cancelRequested = false;
};

// Builds the configured provider, reading the API key from a value already loaded on the UI thread
// (so no credential UI is ever shown from the worker thread).
[[nodiscard]] AiHttpProvider makeConfiguredProvider(std::string secret)
{
	const RuntimeConfiguration runtime = currentConfiguration();
	AiProviderConfiguration configuration;
	configuration.kind = runtime.kind;
	configuration.endpoint = runtime.endpoint;
	configuration.defaultModel = runtime.model;
	const std::shared_ptr<IAiTransport> transport = std::make_shared<WinHttpAiTransport>();
	return AiHttpProvider(configuration, transport, [secret = std::move(secret)]() -> AiResult<std::string> { return secret; });
}

// Runs provider.complete on a background thread while showing a cancellable modal wait, so the UI
// thread never blocks on the network.
[[nodiscard]] AiResult<std::string> runProviderWithProgress(HWND owner, HINSTANCE instance, const std::wstring & caption, AiHttpProvider & provider, const AiProviderRequest & request)
{
	HANDLE doneEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (doneEvent == nullptr)
		return provider.complete(request, {}); // last-resort synchronous fallback

	std::stop_source stopSource;
	AiResult<std::string> result = AiMakeError(AiErrorCode::InternalError, "The AI request did not run.");
	std::thread worker([&provider, &request, &result, &stopSource, doneEvent]() {
		result = provider.complete(request, stopSource.get_token());
		::SetEvent(doneEvent);
	});

	bool cancelled = false;
	{
		AiProgressModal modal(owner, instance, L"Notepad++ AI", caption);
		const bool ownerWasEnabled = owner != nullptr && ::IsWindowEnabled(owner) != FALSE;
		if (ownerWasEnabled)
			::EnableWindow(owner, FALSE);
		cancelled = !modal.waitFor(doneEvent, stopSource);
		if (ownerWasEnabled)
			::EnableWindow(owner, TRUE);
	}
	if (owner != nullptr)
		::SetActiveWindow(owner);

	worker.join();
	::CloseHandle(doneEvent);

	if (cancelled)
		return AiMakeError(AiErrorCode::Cancelled, "The AI request was cancelled.");
	return result;
}

void runAiEdit(HWND parent, HINSTANCE instance, ScintillaEditView & view, Buffer * buffer, std::intptr_t selectionStart, std::intptr_t selectionEnd, std::string instruction)
{
	if (instruction.empty())
	{
		::MessageBoxW(parent, L"The AI instruction must be valid text.", L"Notepad++ AI", MB_OK | MB_ICONWARNING);
		return;
	}

	const AiResult<AiDocumentSnapshot> snapshotResult = captureSnapshot(view, buffer);
	if (!snapshotResult)
	{
		showError(parent, snapshotResult.error());
		return;
	}
	const AiDocumentSnapshot snapshot = snapshotResult.value();
	if (selectionStart < 0 || selectionEnd < 0)
	{
		::MessageBoxW(parent, L"Unable to read the current selection.", L"Notepad++ AI", MB_OK | MB_ICONERROR);
		return;
	}

	AiScopeRequest scopeRequest;
	if (selectionStart != selectionEnd)
		scopeRequest.selection = AiTextRange { static_cast<std::size_t>(selectionStart), static_cast<std::size_t>(selectionEnd) };
	const AiResult<AiScope> scopeResult = AiResolveScope(snapshot, scopeRequest);
	if (!scopeResult)
	{
		showError(parent, scopeResult.error());
		return;
	}
	const AiResult<AiPrompt> promptResult = AiBuildEditPrompt(snapshot, scopeResult.value(), instruction);
	if (!promptResult)
	{
		showError(parent, promptResult.error());
		return;
	}

	// Load (or, when absent, prompt for) the API key on the UI thread so no credential UI is ever
	// shown from the background worker thread.
	const AiResult<std::string> secret = loadOrPromptForSecret(parent);
	if (!secret)
	{
		showError(parent, secret.error());
		return;
	}

	const RuntimeConfiguration runtime = currentConfiguration();
	AiHttpProvider provider = makeConfiguredProvider(secret.value());
	AiProviderRequest request;
	request.model = runtime.model;
	request.messages = { { AiMessageRole::System, promptResult.value().system }, { AiMessageRole::User, promptResult.value().user } };
	request.temperature = 0.0;
	request.maxOutputTokens = 2048;
	const AiResult<std::string> modelResult = runProviderWithProgress(parent, instance, makeActivityMessage(runtime, promptResult.value().includedScope), provider, request);
	if (!modelResult)
	{
		if (modelResult.error().code != AiErrorCode::Cancelled)
			showError(parent, modelResult.error());
		return;
	}

	const AiResult<AiEditPlan> planResult = AiParseAndValidateEditPlan(modelResult.value(), snapshot, promptResult.value().includedScope);
	if (!planResult)
	{
		showError(parent, planResult.error());
		return;
	}
	// The provider returned a plan for the immutable snapshot. Validation above plus the applier's
	// second snapshot check keep automatic application bounded to the requested scope and safely undoable.
	ScintillaAiEditor editor(view, buffer);
	const AiResult<AiApplyResult> applyResult = AiEditApplier::apply(editor, snapshot, planResult.value(), promptResult.value().includedScope);
	if (!applyResult)
		showError(parent, applyResult.error());
}

class InlineAiPrompt;
[[nodiscard]] std::unique_ptr<InlineAiPrompt> & activeInlinePrompt();
void submitInlinePrompt();
void dismissInlinePrompt();

class InlineAiPrompt final
{
public:
	InlineAiPrompt(HWND parent, HINSTANCE instance, ScintillaEditView & view, Buffer * buffer, std::intptr_t selectionStart, std::intptr_t selectionEnd, std::intptr_t caret, std::intptr_t anchor) :
		_parent(parent), _instance(instance), _view(view), _buffer(buffer), _selectionStart(selectionStart), _selectionEnd(selectionEnd), _caret(caret), _anchor(anchor)
	{
	}

	~InlineAiPrompt()
	{
		close();
	}

	[[nodiscard]] bool open()
	{
		const HWND viewWindow = _view.getHSelf();
		if (!::IsWindow(viewWindow))
			return false;

		_label = ::CreateWindowExW(0, L"STATIC", L"AI >", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 0, 0, viewWindow, nullptr, _instance, nullptr);
		_edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, viewWindow, nullptr, _instance, nullptr);
		if (_label == nullptr || _edit == nullptr)
		{
			close();
			return false;
		}

		const HFONT font = reinterpret_cast<HFONT>(::SendMessageW(viewWindow, WM_GETFONT, 0, 0));
		if (font != nullptr)
		{
			::SendMessageW(_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
			::SendMessageW(_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
		}
		::SendMessageW(_edit, EM_SETLIMITTEXT, 4096, 0);
		::SendMessageW(_edit, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Describe the edit — Enter to apply, Esc to cancel"));
		if (::SetWindowSubclass(_edit, editProc, 1, reinterpret_cast<DWORD_PTR>(this)) == FALSE)
		{
			close();
			return false;
		}
		positionControls();
		::SetFocus(_edit);
		return true;
	}

	void submit()
	{
		const std::wstring instructionWide = instruction();
		close();
		const std::string instructionUtf8 = toUtf8(instructionWide);
		if (instructionWide.empty() || instructionUtf8.empty())
			return;
		runAiEdit(_parent, _instance, _view, _buffer, _selectionStart, _selectionEnd, instructionUtf8);
	}

	void close() noexcept
	{
		if (_edit != nullptr && ::IsWindow(_edit))
		{
			::RemoveWindowSubclass(_edit, editProc, 1);
			::DestroyWindow(_edit);
		}
		if (_label != nullptr && ::IsWindow(_label))
			::DestroyWindow(_label);
		_edit = nullptr;
		_label = nullptr;
		if (::IsWindow(_view.getHSelf()) && _view.getCurrentBuffer() == _buffer)
		{
			_view.execute(SCI_SETSEL, static_cast<WPARAM>(_caret), static_cast<LPARAM>(_anchor));
			::SetFocus(_view.getHSelf());
		}
	}

private:
	[[nodiscard]] std::wstring instruction() const
	{
		if (_edit == nullptr || !::IsWindow(_edit))
			return {};
		const int length = ::GetWindowTextLengthW(_edit);
		if (length <= 0)
			return {};
		std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
		::GetWindowTextW(_edit, text.data(), length + 1);
		text.resize(static_cast<std::size_t>(length));
		return text;
	}

	void positionControls()
	{
		const HWND viewWindow = _view.getHSelf();
		RECT client{};
		::GetClientRect(viewWindow, &client);
		const auto currentPosition = _view.execute(SCI_GETCURRENTPOS);
		const LONG caretX = static_cast<LONG>(_view.execute(SCI_POINTXFROMPOSITION, 0, currentPosition));
		const LONG caretY = static_cast<LONG>(_view.execute(SCI_POINTYFROMPOSITION, 0, currentPosition));
		const int textHeight = std::max(1, static_cast<int>(_view.execute(SCI_TEXTHEIGHT, 0, 0)));
		const int rowHeight = std::max(24, textHeight + 8);
		const int margin = 4;
		const int labelWidth = 34;
		int rowX = std::max(margin, static_cast<int>(caretX));
		int rowY = static_cast<int>(caretY) + textHeight;
		if (rowY + rowHeight > static_cast<int>(client.bottom))
			rowY = std::max(0, static_cast<int>(client.bottom) - rowHeight - margin);
		if (static_cast<int>(client.right) - rowX < labelWidth + 160)
			rowX = margin;
		const int rowWidth = std::max(160, static_cast<int>(client.right) - rowX - margin);
		::SetWindowPos(_label, HWND_TOP, rowX, rowY, labelWidth, rowHeight, SWP_SHOWWINDOW);
		::SetWindowPos(_edit, HWND_TOP, rowX + labelWidth, rowY, rowWidth - labelWidth, rowHeight, SWP_SHOWWINDOW);
	}

	static LRESULT CALLBACK editProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
	{
		if (message == WM_GETDLGCODE)
			return DLGC_WANTALLKEYS;
		if (message == WM_KEYDOWN && wParam == VK_RETURN)
		{
			submitInlinePrompt();
			return 0;
		}
		if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
		{
			dismissInlinePrompt();
			return 0;
		}
		if (message == WM_CHAR && (wParam == L'\r' || wParam == 27))
			return 0;
		return ::DefSubclassProc(window, message, wParam, lParam);
	}

	HWND _parent = nullptr;
	HINSTANCE _instance = nullptr;
	ScintillaEditView & _view;
	Buffer * _buffer = nullptr;
	std::intptr_t _selectionStart = 0;
	std::intptr_t _selectionEnd = 0;
	std::intptr_t _caret = 0;
	std::intptr_t _anchor = 0;
	HWND _label = nullptr;
	HWND _edit = nullptr;
};

[[nodiscard]] std::unique_ptr<InlineAiPrompt> & activeInlinePrompt()
{
	static std::unique_ptr<InlineAiPrompt> prompt;
	return prompt;
}

void submitInlinePrompt()
{
	std::unique_ptr<InlineAiPrompt> prompt = std::move(activeInlinePrompt());
	if (prompt)
		prompt->submit();
}

void dismissInlinePrompt()
{
	std::unique_ptr<InlineAiPrompt> prompt = std::move(activeInlinePrompt());
	if (prompt)
		prompt->close();
}

} // namespace

void RunNotepadAiEdit(HWND parent, HINSTANCE instance, ScintillaEditView & view)
{
	Buffer * const buffer = view.getCurrentBuffer();
	if (buffer == nullptr || buffer->isReadOnly())
	{
		::MessageBoxW(parent, L"The current document is read-only.", L"Notepad++ AI", MB_OK | MB_ICONWARNING);
		return;
	}
	if (view.execute(SCI_GETCODEPAGE) != SC_CP_UTF8)
	{
		::MessageBoxW(parent, L"AI editing is currently available only for UTF-8 documents.", L"Notepad++ AI", MB_OK | MB_ICONWARNING);
		return;
	}
	if (view.execute(SCI_GETSELECTIONS) != 1)
	{
		::MessageBoxW(parent, L"AI Edit supports one selection or the whole document. Clear additional selections and try again.", L"Notepad++ AI", MB_OK | MB_ICONWARNING);
		return;
	}

	dismissInlinePrompt();
	const auto selectionStart = view.execute(SCI_GETSELECTIONSTART);
	const auto selectionEnd = view.execute(SCI_GETSELECTIONEND);
	const auto caret = view.execute(SCI_GETCURRENTPOS);
	const auto anchor = view.execute(SCI_GETANCHOR);
	if (selectionStart < 0 || selectionEnd < 0 || caret < 0 || anchor < 0)
	{
		::MessageBoxW(parent, L"Unable to read the current selection.", L"Notepad++ AI", MB_OK | MB_ICONERROR);
		return;
	}

	std::unique_ptr<InlineAiPrompt> prompt = std::make_unique<InlineAiPrompt>(parent, instance, view, buffer, selectionStart, selectionEnd, caret, anchor);
	if (!prompt->open())
	{
		::MessageBoxW(parent, L"Unable to open the inline AI prompt.", L"Notepad++ AI", MB_OK | MB_ICONERROR);
		return;
	}
	activeInlinePrompt() = std::move(prompt);
}

bool HasStoredApiKey()
{
	const RuntimeConfiguration configuration = currentConfiguration();
	WindowsCredentialManagerSecretStore store;
	const AiResult<std::string> existing = store.load(configuration.credentialTarget);
	return existing && !existing.value().empty();
}

bool StoreApiKey(std::string_view apiKeyUtf8, std::wstring & errorMessage)
{
	const RuntimeConfiguration configuration = currentConfiguration();
	WindowsCredentialManagerSecretStore store;

	// Trim surrounding ASCII whitespace so a pasted key with a stray newline still works.
	while (!apiKeyUtf8.empty() && (apiKeyUtf8.front() == ' ' || apiKeyUtf8.front() == '\t' || apiKeyUtf8.front() == '\r' || apiKeyUtf8.front() == '\n'))
		apiKeyUtf8.remove_prefix(1);
	while (!apiKeyUtf8.empty() && (apiKeyUtf8.back() == ' ' || apiKeyUtf8.back() == '\t' || apiKeyUtf8.back() == '\r' || apiKeyUtf8.back() == '\n'))
		apiKeyUtf8.remove_suffix(1);

	if (apiKeyUtf8.empty())
	{
		const AiStatus erased = store.erase(configuration.credentialTarget);
		if (!erased)
		{
			errorMessage = toWide(erased.error().message);
			return false;
		}
		return true;
	}

	if (!AiIsValidUtf8(apiKeyUtf8))
	{
		errorMessage = L"The API key must be valid UTF-8 text.";
		return false;
	}
	for (const char character : apiKeyUtf8)
	{
		if (character == '\r' || character == '\n' || character == '\0')
		{
			errorMessage = L"The API key cannot contain line breaks.";
			return false;
		}
	}

	const AiStatus saved = store.save(configuration.credentialTarget, apiKeyUtf8);
	if (!saved)
	{
		errorMessage = toWide(saved.error().message);
		return false;
	}
	return true;
}

bool TestConnection(HWND parent, HINSTANCE instance, std::wstring & message)
{
	const RuntimeConfiguration runtime = currentConfiguration();
	WindowsCredentialManagerSecretStore store;
	const AiResult<std::string> secret = store.load(runtime.credentialTarget);
	if (!secret || secret.value().empty())
	{
		message = L"No API key is stored yet. Enter your key and click Save first.";
		return false;
	}

	AiHttpProvider provider = makeConfiguredProvider(secret.value());
	AiProviderRequest request;
	request.model = runtime.model;
	request.messages = {
		{ AiMessageRole::System, "You are a connectivity test for a text editor. Answer briefly." },
		{ AiMessageRole::User, "Reply with exactly: OK" }
	};
	request.temperature = 0.0;
	request.maxOutputTokens = 32;

	const AiResult<std::string> result = runProviderWithProgress(parent, instance, L"Testing the AI connection. This can take a while…", provider, request);
	if (!result)
	{
		if (result.error().code == AiErrorCode::Cancelled)
			message = L"The connection test was cancelled.";
		else
			message = toWide(result.error().message);
		return false;
	}

	message = L"Success. " + std::wstring(runtime.providerName) + L" (model \"" + toWide(request.model) + L"\") responded:\n\n" + toWide(AiTruncateUtf8(result.value(), 300));
	return true;
}

} // namespace NppAi
