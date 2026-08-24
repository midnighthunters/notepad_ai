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

#include <Scintilla.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wincred.h>

namespace NppAi
{
namespace
{

constexpr wchar_t credentialTarget[] = L"Notepad++ AI/Gemini";
constexpr char geminiModel[] = "gemini-flash-latest";
constexpr char geminiEndpoint[] = "https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-latest:generateContent";

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

void showError(HWND parent, const AiError & error)
{
	const std::wstring message = toWide(error.message);
	::MessageBoxW(parent, message.c_str(), L"Notepad++ AI", MB_OK | MB_ICONERROR);
}

[[nodiscard]] AiResult<std::string> promptForSecret(HWND parent)
{
	wchar_t userName[CREDUI_MAX_USERNAME_LENGTH + 1]{};
	wchar_t password[CREDUI_MAX_PASSWORD_LENGTH + 1]{};
	BOOL save = FALSE;
	CREDUI_INFOW info{};
	info.cbSize = sizeof(info);
	info.hwndParent = parent;
	info.pszMessageText = L"Enter your Gemini API key. It will be stored only in Windows Credential Manager.";
	info.pszCaptionText = L"Notepad++ AI setup";
	const DWORD result = ::CredUIPromptForCredentialsW(
		&info,
		credentialTarget,
		nullptr,
		0,
		userName,
		static_cast<ULONG>(CREDUI_MAX_USERNAME_LENGTH + 1),
		password,
		static_cast<ULONG>(CREDUI_MAX_PASSWORD_LENGTH + 1),
		&save,
		CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_ALWAYS_SHOW_UI | CREDUI_FLAGS_DO_NOT_PERSIST);
	if (result != NO_ERROR)
		return AiMakeError(AiErrorCode::SecretUnavailable, "No Gemini API key is available.");

	std::string secret = toUtf8(password);
	::SecureZeroMemory(password, sizeof(password));
	if (secret.empty())
		return AiMakeError(AiErrorCode::SecretUnavailable, "The Gemini API key cannot be empty.");
	return secret;
}

[[nodiscard]] AiResult<std::string> loadOrPromptForSecret(HWND parent)
{
	WindowsCredentialManagerSecretStore store;
	const AiResult<std::string> existing = store.load(credentialTarget);
	if (existing && !existing.value().empty())
		return existing.value();

	AiResult<std::string> prompted = promptForSecret(parent);
	if (!prompted)
		return prompted.error();
	const AiStatus saved = store.save(credentialTarget, prompted.value());
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

[[nodiscard]] std::string previewText(std::string_view text)
{
	std::string escaped;
	escaped.reserve(std::min<std::size_t>(text.size(), 512));
	bool truncated = false;
	for (const char character : text)
	{
		if (escaped.size() >= 512)
		{
			truncated = true;
			break;
		}
		switch (character)
		{
		case '\r':
			escaped += "\\r";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\t':
			escaped += "\\t";
			break;
		case '\0':
			escaped += "\\0";
			break;
		default:
			escaped += character;
			break;
		}
	}
	if (truncated)
		escaped += "...";
	return AiTruncateUtf8(escaped, 512);
}

[[nodiscard]] std::wstring makePreview(const AiEditPlan & plan, const AiDiffModel & diff)
{
	std::string preview = "Summary: " + plan.summary + "\n\n" + std::to_string(diff.hunks.size()) + " change(s) are ready.\n\n";
	for (std::size_t index = 0; index < diff.hunks.size(); ++index)
	{
		const AiDiffHunk & hunk = diff.hunks[index];
		const char * kind = hunk.kind == AiDiffKind::Insert ? "Insert" : hunk.kind == AiDiffKind::Delete ? "Delete" : "Replace";
		preview += std::to_string(index + 1) + ". " + kind + " bytes [" + std::to_string(hunk.originalRange.start) + ", " + std::to_string(hunk.originalRange.end) + ")\n";
		preview += "Original: " + previewText(hunk.originalText) + "\n";
		preview += "Replacement: " + previewText(hunk.replacementText) + "\n\n";
		if (preview.size() >= 3800)
		{
			preview += "Additional changes are omitted from this preview.\n";
			break;
		}
	}
	preview += "Apply these changes? You can undo the complete edit with one Undo command.";
	return toWide(AiTruncateUtf8(preview, 4096));
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

	StringDlg instructionDialog;
	instructionDialog.init(instance, parent, L"AI Edit", L"Describe the edit to make", L"", 4096, nullptr, true);
	wchar_t * const instructionWide = reinterpret_cast<wchar_t *>(instructionDialog.doDialog());
	if (instructionWide == nullptr || instructionWide[0] == L'\0')
		return;
	const std::string instruction = toUtf8(instructionWide);
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
	AiScopeRequest scopeRequest;
	const auto selectionStart = view.execute(SCI_GETSELECTIONSTART);
	const auto selectionEnd = view.execute(SCI_GETSELECTIONEND);
	if (selectionStart < 0 || selectionEnd < 0)
	{
		::MessageBoxW(parent, L"Unable to read the current selection.", L"Notepad++ AI", MB_OK | MB_ICONERROR);
		return;
	}
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

	AiProviderConfiguration configuration;
	configuration.kind = AiProviderKind::GeminiGenerateContent;
	configuration.endpoint = geminiEndpoint;
	configuration.defaultModel = geminiModel;
	const std::shared_ptr<IAiTransport> transport = std::make_shared<WinHttpAiTransport>();
	AiHttpProvider provider(configuration, transport, [parent]() { return loadOrPromptForSecret(parent); });
	AiProviderRequest request;
	request.model = geminiModel;
	request.messages = { { AiMessageRole::System, promptResult.value().system }, { AiMessageRole::User, promptResult.value().user } };
	request.temperature = 0.0;
	request.maxOutputTokens = 4096;
	const AiResult<std::string> modelResult = provider.complete(request, {});
	if (!modelResult)
	{
		showError(parent, modelResult.error());
		return;
	}

	const AiResult<AiEditPlan> planResult = AiParseAndValidateEditPlan(modelResult.value(), snapshot, promptResult.value().includedScope);
	if (!planResult)
	{
		showError(parent, planResult.error());
		return;
	}
	const AiResult<AiDiffModel> diffResult = AiBuildDiffModel(snapshot, planResult.value(), promptResult.value().includedScope);
	if (!diffResult)
	{
		showError(parent, diffResult.error());
		return;
	}
	const std::wstring preview = makePreview(planResult.value(), diffResult.value());
	if (::MessageBoxW(parent, preview.c_str(), L"Review AI changes", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	ScintillaAiEditor editor(view, buffer);
	const AiResult<AiApplyResult> applyResult = AiEditApplier::apply(editor, snapshot, planResult.value(), promptResult.value().includedScope);
	if (!applyResult)
	{
		showError(parent, applyResult.error());
		return;
	}
}

} // namespace NppAi
